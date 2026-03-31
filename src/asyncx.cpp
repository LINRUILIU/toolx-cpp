#include "asyncx.h"

namespace asyncx
{

    namespace
    {

        std::size_t ResolveWorkerCount(std::size_t requested)
        {
            if (requested > 0)
            {
                return requested;
            }

            const auto detected = sysx::thread::HardwareConcurrency();
            return (detected == 0) ? 1u : detected;
        }

    } // namespace

    const char *ToString(ErrorKind kind) noexcept
    {
        switch (kind)
        {
        case ErrorKind::None:
            return "none";
        case ErrorKind::InvalidArgument:
            return "invalid_argument";
        case ErrorKind::Timeout:
            return "timeout";
        case ErrorKind::QueueFull:
            return "queue_full";
        case ErrorKind::QueueClosed:
            return "queue_closed";
        case ErrorKind::NotRunning:
            return "not_running";
        case ErrorKind::NotFound:
            return "not_found";
        case ErrorKind::Internal:
            return "internal";
        default:
            return "unknown";
        }
    }

    const char *ToString(StopMode mode) noexcept
    {
        switch (mode)
        {
        case StopMode::Drain:
            return "drain";
        case StopMode::CancelPending:
            return "cancel_pending";
        default:
            return "unknown";
        }
    }

    const char *ToString(BackpressurePolicy policy) noexcept
    {
        switch (policy)
        {
        case BackpressurePolicy::Block:
            return "block";
        case BackpressurePolicy::Reject:
            return "reject";
        default:
            return "unknown";
        }
    }

    const char *ToString(TaskPriority priority) noexcept
    {
        switch (priority)
        {
        case TaskPriority::High:
            return "high";
        case TaskPriority::Normal:
            return "normal";
        case TaskPriority::Low:
            return "low";
        default:
            return "unknown";
        }
    }

    Status OkStatus()
    {
        Status status;
        status.ok = true;
        return status;
    }

    Status MakeErrorStatus(ErrorKind kind, std::string message, bool retryable)
    {
        Status status;
        status.ok = false;
        status.error.kind = kind;
        status.error.retryable = retryable;
        status.error.message = std::move(message);
        return status;
    }

    ThreadPool::ThreadPool(PoolOptions options)
        : options_(std::move(options))
    {
        options_.worker_count = ResolveWorkerCount(options_.worker_count);
        if (options_.queue_capacity == 0)
        {
            options_.queue_capacity = 1;
        }
        backpressure_policy_ = options_.backpressure_policy;

        if (options_.start_immediately)
        {
            (void)Start();
        }
    }

    ThreadPool::~ThreadPool()
    {
        (void)Stop(StopMode::Drain);
        (void)Join();
    }

    Status ThreadPool::Start()
    {
        {
            std::lock_guard<sysx::sync::Mutex> lock(mu_);
            if (running_)
            {
                return OkStatus();
            }

            stop_mode_ = StopMode::Drain;
            stop_requested_ = false;
            scheduler_stop_requested_ = false;
            accepting_ = true;
            running_ = true;
            active_workers_ = 0;
            next_task_sequence_ = 1;
            next_dispatch_slot_ = 0;
        }

        try
        {
            workers_.reserve(options_.worker_count);
            for (std::size_t i = 0; i < options_.worker_count; ++i)
            {
                workers_.emplace_back([this]()
                                      { WorkerLoop(); });
            }

            scheduler_thread_ = sysx::thread::Thread([this]()
                                                     { SchedulerLoop(); });
        }
        catch (...)
        {
            {
                std::lock_guard<sysx::sync::Mutex> lock(mu_);
                accepting_ = false;
                stop_requested_ = true;
                scheduler_stop_requested_ = true;
            }
            cv_not_empty_.notify_all();
            cv_not_full_.notify_all();
            cv_scheduler_.notify_all();

            if (scheduler_thread_.Joinable())
            {
                scheduler_thread_.Join();
            }

            for (auto &worker : workers_)
            {
                if (worker.Joinable())
                {
                    worker.Join();
                }
            }
            workers_.clear();

            {
                std::lock_guard<sysx::sync::Mutex> lock(mu_);
                running_ = false;
                stop_requested_ = false;
                scheduler_stop_requested_ = false;
                stop_mode_ = StopMode::Drain;
            }

            return MakeErrorStatus(ErrorKind::Internal, "failed to start worker threads", false);
        }

        return OkStatus();
    }

    Status ThreadPool::Stop(StopMode mode)
    {
        {
            std::lock_guard<sysx::sync::Mutex> lock(mu_);
            if (!running_)
            {
                return OkStatus();
            }

            stop_mode_ = mode;
            accepting_ = false;
            stop_requested_ = true;
            scheduler_stop_requested_ = true;

            if (!scheduled_tasks_.empty())
            {
                rejected_.fetch_add(static_cast<std::uint64_t>(scheduled_tasks_.size()), std::memory_order_relaxed);
                scheduled_cancelled_.fetch_add(static_cast<std::uint64_t>(scheduled_tasks_.size()), std::memory_order_relaxed);
                scheduled_tasks_.clear();
            }

            if (mode == StopMode::CancelPending && !queue_.empty())
            {
                rejected_.fetch_add(static_cast<std::uint64_t>(queue_.size()), std::memory_order_relaxed);
                queue_.clear();
            }

            if (IsIdleLocked())
            {
                cv_idle_.notify_all();
            }
        }

        cv_not_empty_.notify_all();
        cv_not_full_.notify_all();
        cv_scheduler_.notify_all();
        return OkStatus();
    }

    Status ThreadPool::Join()
    {
        std::vector<sysx::thread::Thread> local_workers;
        sysx::thread::Thread local_scheduler;
        {
            std::lock_guard<sysx::sync::Mutex> lock(mu_);
            if (running_ && !stop_requested_)
            {
                accepting_ = false;
                stop_requested_ = true;
                scheduler_stop_requested_ = true;
                if (!scheduled_tasks_.empty())
                {
                    rejected_.fetch_add(static_cast<std::uint64_t>(scheduled_tasks_.size()), std::memory_order_relaxed);
                    scheduled_cancelled_.fetch_add(static_cast<std::uint64_t>(scheduled_tasks_.size()), std::memory_order_relaxed);
                    scheduled_tasks_.clear();
                }
            }

            local_workers.swap(workers_);
            local_scheduler = std::move(scheduler_thread_);
        }

        cv_not_empty_.notify_all();
        cv_not_full_.notify_all();
        cv_scheduler_.notify_all();

        if (local_scheduler.Joinable())
        {
            local_scheduler.Join();
        }

        for (auto &worker : local_workers)
        {
            if (worker.Joinable())
            {
                worker.Join();
            }
        }

        {
            std::lock_guard<sysx::sync::Mutex> lock(mu_);
            running_ = false;
            accepting_ = false;
            stop_requested_ = false;
            scheduler_stop_requested_ = false;
            stop_mode_ = StopMode::Drain;
            if (IsIdleLocked())
            {
                cv_idle_.notify_all();
            }
        }

        cv_not_full_.notify_all();
        return OkStatus();
    }

    Status ThreadPool::StopAndJoin(StopMode mode)
    {
        const Status stop_status = Stop(mode);
        if (!stop_status.ok)
        {
            return stop_status;
        }
        return Join();
    }

    bool ThreadPool::IsRunning() const noexcept
    {
        std::lock_guard<sysx::sync::Mutex> lock(mu_);
        return running_;
    }

    bool ThreadPool::IsAccepting() const noexcept
    {
        std::lock_guard<sysx::sync::Mutex> lock(mu_);
        return accepting_;
    }

    bool ThreadPool::IsIdle() const noexcept
    {
        std::lock_guard<sysx::sync::Mutex> lock(mu_);
        return IsIdleLocked();
    }

    std::size_t ThreadPool::QueueSize() const noexcept
    {
        std::lock_guard<sysx::sync::Mutex> lock(mu_);
        return queue_.size();
    }

    std::size_t ThreadPool::QueueCapacity() const noexcept
    {
        return options_.queue_capacity;
    }

    std::size_t ThreadPool::ScheduledCount() const noexcept
    {
        std::lock_guard<sysx::sync::Mutex> lock(mu_);
        return scheduled_tasks_.size();
    }

    std::size_t ThreadPool::ActiveWorkerCount() const noexcept
    {
        std::lock_guard<sysx::sync::Mutex> lock(mu_);
        return active_workers_;
    }

    std::size_t ThreadPool::WorkerCount() const noexcept
    {
        std::lock_guard<sysx::sync::Mutex> lock(mu_);
        return workers_.size();
    }

    BackpressurePolicy ThreadPool::GetBackpressurePolicy() const noexcept
    {
        std::lock_guard<sysx::sync::Mutex> lock(mu_);
        return backpressure_policy_;
    }

    void ThreadPool::SetBackpressurePolicy(BackpressurePolicy policy) noexcept
    {
        std::lock_guard<sysx::sync::Mutex> lock(mu_);
        backpressure_policy_ = policy;
    }

    Stats ThreadPool::GetStats() const noexcept
    {
        Stats stats;
        stats.submitted = submitted_.load(std::memory_order_relaxed);
        stats.completed = completed_.load(std::memory_order_relaxed);
        stats.rejected = rejected_.load(std::memory_order_relaxed);
        stats.timed_out = timed_out_.load(std::memory_order_relaxed);
        stats.backpressure_rejected = backpressure_rejected_.load(std::memory_order_relaxed);
        return stats;
    }

    MetricsSnapshot ThreadPool::GetMetricsSnapshot() const noexcept
    {
        MetricsSnapshot snapshot;
        {
            std::lock_guard<sysx::sync::Mutex> lock(mu_);
            snapshot.running = running_;
            snapshot.accepting = accepting_;
            snapshot.idle = IsIdleLocked();
            snapshot.worker_count = workers_.size();
            snapshot.active_workers = active_workers_;
            snapshot.queue_size = queue_.size();
            snapshot.queue_capacity = options_.queue_capacity;
            snapshot.scheduled_count = scheduled_tasks_.size();
            snapshot.backpressure_policy = backpressure_policy_;
            snapshot.scheduler.pending = scheduled_tasks_.size();
        }

        snapshot.execution.submitted = submitted_.load(std::memory_order_relaxed);
        snapshot.execution.completed = completed_.load(std::memory_order_relaxed);
        snapshot.execution.rejected = rejected_.load(std::memory_order_relaxed);
        snapshot.execution.timed_out = timed_out_.load(std::memory_order_relaxed);
        snapshot.execution.backpressure_rejected = backpressure_rejected_.load(std::memory_order_relaxed);

        snapshot.scheduler.created = scheduled_created_.load(std::memory_order_relaxed);
        snapshot.scheduler.fired = scheduled_fired_.load(std::memory_order_relaxed);
        snapshot.scheduler.cancelled = scheduled_cancelled_.load(std::memory_order_relaxed);
        snapshot.scheduler.periodic_rescheduled = periodic_rescheduled_.load(std::memory_order_relaxed);

        return snapshot;
    }

    Stats ThreadPool::ResetStats() noexcept
    {
        Stats previous;
        previous.submitted = submitted_.exchange(0, std::memory_order_relaxed);
        previous.completed = completed_.exchange(0, std::memory_order_relaxed);
        previous.rejected = rejected_.exchange(0, std::memory_order_relaxed);
        previous.timed_out = timed_out_.exchange(0, std::memory_order_relaxed);
        previous.backpressure_rejected = backpressure_rejected_.exchange(0, std::memory_order_relaxed);

        scheduled_created_.store(0, std::memory_order_relaxed);
        scheduled_fired_.store(0, std::memory_order_relaxed);
        scheduled_cancelled_.store(0, std::memory_order_relaxed);
        periodic_rescheduled_.store(0, std::memory_order_relaxed);
        return previous;
    }

    Status ThreadPool::Post(std::function<void()> task)
    {
        return PostWithPriority(TaskPriority::Normal, std::move(task));
    }

    Status ThreadPool::TryPost(std::function<void()> task)
    {
        return TryPostWithPriority(TaskPriority::Normal, std::move(task));
    }

    Status ThreadPool::PostUntil(std::chrono::steady_clock::time_point deadline, std::function<void()> task)
    {
        return PostWithPriorityUntil(deadline, TaskPriority::Normal, std::move(task));
    }

    Status ThreadPool::PostWithPriority(TaskPriority priority, std::function<void()> task)
    {
        return EnqueueTask(std::move(task), std::nullopt, ShouldWaitOnBackpressure(), priority);
    }

    Status ThreadPool::TryPostWithPriority(TaskPriority priority, std::function<void()> task)
    {
        return EnqueueTask(std::move(task), std::nullopt, false, priority);
    }

    Status ThreadPool::PostWithPriorityUntil(std::chrono::steady_clock::time_point deadline,
                                             TaskPriority priority,
                                             std::function<void()> task)
    {
        return EnqueueTask(std::move(task), deadline, true, priority);
    }

    Result<std::uint64_t> ThreadPool::PostDelayedUntil(std::chrono::steady_clock::time_point due_time, std::function<void()> task)
    {
        Result<std::uint64_t> out;
        if (!task)
        {
            rejected_.fetch_add(1, std::memory_order_relaxed);
            out.ok = false;
            out.error = MakeErrorStatus(ErrorKind::InvalidArgument, "task must not be empty", false).error;
            return out;
        }

        std::lock_guard<sysx::sync::Mutex> lock(mu_);
        if (!running_)
        {
            rejected_.fetch_add(1, std::memory_order_relaxed);
            out.ok = false;
            out.error = MakeErrorStatus(ErrorKind::NotRunning, "thread pool is not running", true).error;
            return out;
        }
        if (!accepting_)
        {
            rejected_.fetch_add(1, std::memory_order_relaxed);
            out.ok = false;
            out.error = MakeErrorStatus(ErrorKind::QueueClosed, "thread pool is not accepting scheduled tasks", true).error;
            return out;
        }

        ScheduledTask entry;
        entry.id = next_scheduled_id_++;
        entry.task = std::move(task);
        entry.due = due_time;
        entry.interval = std::chrono::steady_clock::duration::zero();
        entry.periodic = false;
        scheduled_tasks_.push_back(std::move(entry));
        scheduled_created_.fetch_add(1, std::memory_order_relaxed);

        out.ok = true;
        out.value = scheduled_tasks_.back().id;
        cv_scheduler_.notify_one();
        return out;
    }

    Result<std::uint64_t> ThreadPool::ScheduleEvery(std::chrono::steady_clock::duration interval,
                                                    std::function<void()> task,
                                                    bool run_immediately)
    {
        Result<std::uint64_t> out;
        if (!task)
        {
            rejected_.fetch_add(1, std::memory_order_relaxed);
            out.ok = false;
            out.error = MakeErrorStatus(ErrorKind::InvalidArgument, "task must not be empty", false).error;
            return out;
        }
        if (interval <= std::chrono::steady_clock::duration::zero())
        {
            out.ok = false;
            out.error = MakeErrorStatus(ErrorKind::InvalidArgument, "interval must be positive", false).error;
            return out;
        }

        std::lock_guard<sysx::sync::Mutex> lock(mu_);
        if (!running_)
        {
            rejected_.fetch_add(1, std::memory_order_relaxed);
            out.ok = false;
            out.error = MakeErrorStatus(ErrorKind::NotRunning, "thread pool is not running", true).error;
            return out;
        }
        if (!accepting_)
        {
            rejected_.fetch_add(1, std::memory_order_relaxed);
            out.ok = false;
            out.error = MakeErrorStatus(ErrorKind::QueueClosed, "thread pool is not accepting scheduled tasks", true).error;
            return out;
        }

        ScheduledTask entry;
        entry.id = next_scheduled_id_++;
        entry.task = std::move(task);
        entry.due = run_immediately ? sysx::time::SteadyNow() : (sysx::time::SteadyNow() + interval);
        entry.interval = interval;
        entry.periodic = true;
        scheduled_tasks_.push_back(std::move(entry));
        scheduled_created_.fetch_add(1, std::memory_order_relaxed);

        out.ok = true;
        out.value = scheduled_tasks_.back().id;
        cv_scheduler_.notify_one();
        return out;
    }

    Status ThreadPool::CancelScheduled(std::uint64_t task_id)
    {
        if (task_id == 0)
        {
            return MakeErrorStatus(ErrorKind::InvalidArgument, "task_id must not be zero", false);
        }

        std::lock_guard<sysx::sync::Mutex> lock(mu_);
        auto it = FindScheduledByIdLocked(task_id);
        if (it == scheduled_tasks_.end())
        {
            return MakeErrorStatus(ErrorKind::NotFound, "scheduled task was not found", false);
        }

        scheduled_tasks_.erase(it);
        rejected_.fetch_add(1, std::memory_order_relaxed);
        scheduled_cancelled_.fetch_add(1, std::memory_order_relaxed);
        if (IsIdleLocked())
        {
            cv_idle_.notify_all();
        }
        cv_scheduler_.notify_all();
        return OkStatus();
    }

    Status ThreadPool::WaitForIdle()
    {
        return WaitForIdleImpl(std::nullopt);
    }

    Status ThreadPool::WaitForIdleUntil(std::chrono::steady_clock::time_point deadline)
    {
        return WaitForIdleImpl(deadline);
    }

    Status ThreadPool::EnqueueTask(Task task,
                                   OptionalDeadline deadline,
                                   bool wait_for_slot,
                                   TaskPriority priority)
    {
        if (!task)
        {
            rejected_.fetch_add(1, std::memory_order_relaxed);
            return MakeErrorStatus(ErrorKind::InvalidArgument, "task must not be empty", false);
        }

        std::unique_lock<sysx::sync::Mutex> lock(mu_);

        if (!running_)
        {
            rejected_.fetch_add(1, std::memory_order_relaxed);
            return MakeErrorStatus(ErrorKind::NotRunning, "thread pool is not running", true);
        }

        if (!accepting_)
        {
            rejected_.fetch_add(1, std::memory_order_relaxed);
            return MakeErrorStatus(ErrorKind::QueueClosed, "thread pool is not accepting new tasks", true);
        }

        auto has_room = [this]()
        {
            return queue_.size() < options_.queue_capacity;
        };

        if (!has_room())
        {
            if (!wait_for_slot)
            {
                rejected_.fetch_add(1, std::memory_order_relaxed);
                backpressure_rejected_.fetch_add(1, std::memory_order_relaxed);
                return MakeErrorStatus(ErrorKind::QueueFull, "queue is full", true);
            }

            if (deadline.has_value())
            {
                while (!has_room())
                {
                    if (!running_ || !accepting_)
                    {
                        rejected_.fetch_add(1, std::memory_order_relaxed);
                        return MakeErrorStatus(ErrorKind::QueueClosed, "thread pool stopped while waiting for queue slot", true);
                    }

                    if (cv_not_full_.wait_until(lock, *deadline) == std::cv_status::timeout)
                    {
                        if (!has_room())
                        {
                            timed_out_.fetch_add(1, std::memory_order_relaxed);
                            return MakeErrorStatus(ErrorKind::Timeout, "queue wait timed out", true);
                        }
                    }
                }
            }
            else
            {
                cv_not_full_.wait(lock, [this, &has_room]()
                                  { return has_room() || !running_ || !accepting_; });

                if (!running_ || !accepting_)
                {
                    rejected_.fetch_add(1, std::memory_order_relaxed);
                    return MakeErrorStatus(ErrorKind::QueueClosed, "thread pool stopped while waiting for queue slot", true);
                }
            }
        }

        QueuedTask queued;
        queued.task = std::move(task);
        queued.priority = priority;
        queued.sequence = next_task_sequence_++;
        queue_.push_back(std::move(queued));
        submitted_.fetch_add(1, std::memory_order_relaxed);

        lock.unlock();
        cv_not_empty_.notify_one();
        return OkStatus();
    }

    Status ThreadPool::WaitForIdleImpl(OptionalDeadline deadline)
    {
        std::unique_lock<sysx::sync::Mutex> lock(mu_);

        if (!deadline.has_value())
        {
            cv_idle_.wait(lock, [this]()
                          { return IsIdleLocked(); });
            return OkStatus();
        }

        if (!cv_idle_.wait_until(lock, *deadline, [this]()
                                 { return IsIdleLocked(); }))
        {
            timed_out_.fetch_add(1, std::memory_order_relaxed);
            return MakeErrorStatus(ErrorKind::Timeout, "wait for idle timed out", true);
        }

        return OkStatus();
    }

    bool ThreadPool::IsIdleLocked() const noexcept
    {
        return queue_.empty() && active_workers_ == 0 && scheduled_tasks_.empty();
    }

    bool ThreadPool::ShouldWaitOnBackpressure() const noexcept
    {
        std::lock_guard<sysx::sync::Mutex> lock(mu_);
        return backpressure_policy_ == BackpressurePolicy::Block;
    }

    std::vector<ThreadPool::ScheduledTask>::iterator ThreadPool::FindScheduledByIdLocked(std::uint64_t task_id)
    {
        for (auto it = scheduled_tasks_.begin(); it != scheduled_tasks_.end(); ++it)
        {
            if (it->id == task_id)
            {
                return it;
            }
        }
        return scheduled_tasks_.end();
    }

    std::vector<ThreadPool::ScheduledTask>::iterator ThreadPool::FindNextDueLocked()
    {
        if (scheduled_tasks_.empty())
        {
            return scheduled_tasks_.end();
        }

        auto best = scheduled_tasks_.begin();
        for (auto it = scheduled_tasks_.begin(); it != scheduled_tasks_.end(); ++it)
        {
            if (it->due < best->due)
            {
                best = it;
            }
        }
        return best;
    }

    void ThreadPool::SchedulerLoop()
    {
        for (;;)
        {
            Task dispatch_task;
            {
                std::unique_lock<sysx::sync::Mutex> lock(mu_);

                while (!scheduler_stop_requested_ && running_ && scheduled_tasks_.empty())
                {
                    cv_scheduler_.wait(lock);
                }

                if (scheduler_stop_requested_ || !running_)
                {
                    return;
                }

                auto it = FindNextDueLocked();
                if (it == scheduled_tasks_.end())
                {
                    continue;
                }

                const auto now = sysx::time::SteadyNow();
                if (it->due > now)
                {
                    cv_scheduler_.wait_until(lock, it->due);
                    continue;
                }

                dispatch_task = it->task;
                if (it->periodic)
                {
                    it->due = now + it->interval;
                    periodic_rescheduled_.fetch_add(1, std::memory_order_relaxed);
                }
                else
                {
                    scheduled_tasks_.erase(it);
                }
            }

            const Status dispatch_status = EnqueueTask(std::move(dispatch_task),
                                                       std::nullopt,
                                                       true,
                                                       TaskPriority::Normal);
            if (!dispatch_status.ok)
            {
                if (dispatch_status.error.kind == ErrorKind::QueueClosed ||
                    dispatch_status.error.kind == ErrorKind::NotRunning)
                {
                    return;
                }
            }
            else
            {
                scheduled_fired_.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }

    void ThreadPool::WorkerLoop()
    {
        constexpr TaskPriority kDispatchSlots[] = {
            TaskPriority::High,
            TaskPriority::High,
            TaskPriority::High,
            TaskPriority::High,
            TaskPriority::Normal,
            TaskPriority::Normal,
            TaskPriority::Low,
        };

        for (;;)
        {
            Task task;
            {
                std::unique_lock<sysx::sync::Mutex> lock(mu_);
                cv_not_empty_.wait(lock, [this]()
                                   { return stop_requested_ || !queue_.empty(); });

                if (queue_.empty())
                {
                    if (stop_requested_)
                    {
                        break;
                    }
                    continue;
                }

                auto find_oldest_with_priority = [this](TaskPriority priority)
                {
                    auto chosen = queue_.end();
                    for (auto it = queue_.begin(); it != queue_.end(); ++it)
                    {
                        if (it->priority != priority)
                        {
                            continue;
                        }
                        if (chosen == queue_.end() || it->sequence < chosen->sequence)
                        {
                            chosen = it;
                        }
                    }
                    return chosen;
                };

                // Weighted slots preserve high-priority bias while preventing starvation.
                auto best = queue_.end();
                for (std::size_t offset = 0; offset < std::size(kDispatchSlots); ++offset)
                {
                    const std::size_t slot = (next_dispatch_slot_ + offset) % std::size(kDispatchSlots);
                    auto candidate = find_oldest_with_priority(kDispatchSlots[slot]);
                    if (candidate != queue_.end())
                    {
                        best = candidate;
                        next_dispatch_slot_ = (slot + 1) % std::size(kDispatchSlots);
                        break;
                    }
                }

                if (best == queue_.end())
                {
                    best = queue_.begin();
                    for (auto it = queue_.begin(); it != queue_.end(); ++it)
                    {
                        if (it->sequence < best->sequence)
                        {
                            best = it;
                        }
                    }
                    next_dispatch_slot_ = (next_dispatch_slot_ + 1) % std::size(kDispatchSlots);
                }

                task = std::move(best->task);
                queue_.erase(best);
                ++active_workers_;
                cv_not_full_.notify_one();
            }

            try
            {
                task();
            }
            catch (...)
            {
                // Task exceptions are intentionally swallowed to keep workers alive.
            }

            completed_.fetch_add(1, std::memory_order_relaxed);

            {
                std::lock_guard<sysx::sync::Mutex> lock(mu_);
                if (active_workers_ > 0)
                {
                    --active_workers_;
                }
                if (IsIdleLocked())
                {
                    cv_idle_.notify_all();
                }
            }
        }
    }

} // namespace asyncx
