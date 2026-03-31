#include <chrono>
#include <iostream>

#include "sysx.h"

int main()
{
    std::cout << "sysx example\n";
    std::cout << "os=" << sysx::ToString(sysx::CurrentOs()) << "\n";
    std::cout << "compiler=" << sysx::ToString(sysx::CurrentCompiler()) << "\n";

    const auto begin = sysx::time::SteadyNow();
    sysx::time::SleepFor(std::chrono::milliseconds(5));
    const auto end = sysx::time::SteadyNow();
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count();
    std::cout << "elapsed_ms=" << elapsed_ms << "\n";

    const auto ok = sysx::OkStatus();
    std::cout << "ok_status=" << (ok.ok ? "true" : "false") << "\n";

    auto worker = sysx::thread::Thread([]()
                                       {
        const auto status = sysx::MakeErrorStatus(sysx::ErrorDomain::System, 0, "worker ok");
        (void)status; });
    if (worker.Joinable())
    {
        worker.Join();
    }

    return 0;
}
