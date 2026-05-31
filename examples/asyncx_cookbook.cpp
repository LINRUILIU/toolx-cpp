#include "asyncx.h"

#include <chrono>
#include <iostream>
#include <vector>

int main()
{
    asyncx::ThreadPool pool(asyncx::PoolOptions{2, 16, true, asyncx::BackpressurePolicy::Block});

    // Scenario 1: Submit returns a future for value-producing tasks.
    auto computed = pool.Submit([] { return 21 * 2; });
    if (computed.ok)
    {
        std::cout << "submit=" << computed.value.get() << "\n";
    }

    // Scenario 2: priority posts are fire-and-forget work items.
    int posted = 0;
    pool.PostWithPriority(asyncx::TaskPriority::High, [&posted] { posted += 1; });
    pool.WaitForIdle();
    std::cout << "posted=" << posted << "\n";

    // Scenario 3: cancellation tokens let tasks opt out cooperatively.
    asyncx::CancellationSource source;
    auto status = pool.PostWithOptions({asyncx::TaskPriority::Normal, {}, source.Token(), "cooperative"},
                                       [](asyncx::CancellationToken token)
                                       {
                                           if (token.IsCancellationRequested())
                                           {
                                               return;
                                           }
                                       });
    std::cout << "post-with-token=" << status.ok << "\n";

    // Scenario 4: TaskGroup batches related tasks and reports outcome counts.
    asyncx::TaskGroup group(pool);
    group.Submit({}, [](asyncx::CancellationToken) {});
    group.Submit({}, [](asyncx::CancellationToken) {});
    group.Wait();
    const auto stats = group.Stats();
    std::cout << "group submitted=" << stats.submitted << " completed=" << stats.completed << "\n";

    // Scenario 5: wait helpers give one call site for many futures.
    std::vector<std::future<int>> futures;
    futures.push_back(pool.Submit([] { return 1; }).value);
    futures.push_back(pool.Submit([] { return 2; }).value);
    asyncx::WaitAll(futures);

    pool.StopAndJoin(asyncx::StopMode::Drain);
    return 0;
}
