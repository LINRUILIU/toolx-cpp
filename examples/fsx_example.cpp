#include <iostream>

#include "fsx.h"

int main()
{
    fsx::BatchPlan plan;
    plan.AddAtomicWrite("temp/fsx/a.txt", "hello\n")
        .AddAtomicWrite("temp/fsx/b.txt", "world\n")
        .AddRename("temp/fsx/b.txt", "temp/fsx/c.txt");

    fsx::RunOptions options;
    options.rollback_mode = fsx::RollbackMode::BestEffort;
    options.conflict_policy = fsx::ConflictPolicy::Overwrite;
    options.journal_path = "temp/fsx/run.journal";

    const fsx::RunResult result = fsx::Run(plan, options);
    if (!result.ok)
    {
        std::cerr << "fsx failed: " << result.error << '\n';
        for (const auto &step : result.steps)
        {
            std::cerr << "step=" << step.step
                      << " op=" << fsx::ToString(step.op)
                      << " ok=" << step.ok
                      << " rolled_back=" << step.rolled_back
                      << " err=" << step.error << '\n';
        }

        if (!options.journal_path.empty())
        {
            const fsx::RunResult recovered = fsx::RecoverFromJournal(options.journal_path);
            std::cerr << "recover ok=" << recovered.ok
                      << " completed=" << recovered.completed_steps
                      << " rolled_back=" << recovered.rolled_back_steps << '\n';
        }
        return 2;
    }

    std::cout << "fsx completed steps=" << result.completed_steps
              << " skipped=" << result.skipped_steps
              << " rolled_back=" << result.rolled_back_steps << '\n';
    return 0;
}
