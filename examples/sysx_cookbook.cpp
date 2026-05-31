#include "sysx.h"

#include <chrono>
#include <iostream>

int main()
{
    // Scenario 1: compile-time platform and compiler helpers.
    std::cout << "os=" << sysx::ToString(sysx::CurrentOs()) << " compiler=" << sysx::ToString(sysx::CurrentCompiler())
              << "\n";

    // Scenario 2: normalized system/network errors have stable categories.
    const auto status = sysx::MakeErrorStatus(sysx::ErrorDomain::System, 2, "example failure");
    std::cout << "status=" << status.ok << " kind=" << sysx::ToString(status.error.kind) << "\n";

    // Scenario 3: time helpers avoid mixing steady and system clocks.
    const auto start = sysx::time::SteadyNow();
    sysx::time::SleepFor(std::chrono::milliseconds(1));
    std::cout << "slept=" << (sysx::time::SteadyNow() >= start) << "\n";

    // Scenario 4: Thread is a small movable wrapper around std::thread.
    int value = 0;
    sysx::thread::Thread worker([&value] { value = 7; });
    if (worker.Joinable())
    {
        worker.Join();
    }
    std::cout << "thread-value=" << value << " hw=" << sysx::thread::HardwareConcurrency() << "\n";
    return 0;
}
