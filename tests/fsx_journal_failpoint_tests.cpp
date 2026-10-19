#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <system_error>
#include <vector>

#include "fsx.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <sys/wait.h>
#endif

namespace
{
namespace fs = std::filesystem;

struct ExpectedFile
{
    fs::path relative_path;
    std::string text;
};

struct Scenario
{
    std::string name;
    std::vector<ExpectedFile> expected_files;
};

std::string ReadText(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::string ShellQuote(const std::string& value)
{
#if defined(_WIN32)
    std::string out = "\"";
    for (const char ch : value)
    {
        if (ch == '\"')
        {
            out += "\\\"";
        }
        else
        {
            out.push_back(ch);
        }
    }
    out.push_back('\"');
    return out;
#else
    std::string out = "'";
    for (const char ch : value)
    {
        if (ch == '\'')
        {
            out += "'\\''";
        }
        else
        {
            out.push_back(ch);
        }
    }
    out.push_back('\'');
    return out;
#endif
}

int RunFailpointChild(const std::string& child, const std::string& scenario, const fs::path& root, std::string* error)
{
#if defined(_WIN32)
    const fs::path child_path(child);
    const std::wstring command_line = L"\"" + child_path.wstring() + L"\" \"" +
                                      std::wstring(scenario.begin(), scenario.end()) + L"\" \"" + root.wstring() +
                                      L"\"";
    std::vector<wchar_t> mutable_command(command_line.begin(), command_line.end());
    mutable_command.push_back(L'\0');

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(child_path.c_str(), mutable_command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                        nullptr, &startup, &process))
    {
        if (error != nullptr)
        {
            *error = std::system_category().message(static_cast<int>(GetLastError()));
        }
        return -1;
    }

    const DWORD wait = WaitForSingleObject(process.hProcess, 30000);
    DWORD exit_code = 0;
    const bool exited = wait == WAIT_OBJECT_0 && GetExitCodeProcess(process.hProcess, &exit_code) != 0;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    if (!exited)
    {
        if (error != nullptr)
        {
            *error = wait == WAIT_TIMEOUT ? "timed out" : "failed while waiting for child process";
        }
        return -1;
    }
    return static_cast<int>(exit_code);
#else
    const std::string command = ShellQuote(child) + " " + ShellQuote(scenario) + " " + ShellQuote(root.string());
    const int code = std::system(command.c_str());
    if (code == -1 && error != nullptr)
    {
        *error = "failed to start child process";
    }
    return code != -1 && WIFEXITED(code) ? WEXITSTATUS(code) : -1;
#endif
}

[[noreturn]] void Fail(const std::string& message)
{
    std::cerr << message << "\n";
    std::exit(1);
}

void VerifyNoStagingFiles(const fs::path& root)
{
    std::error_code ec;
    for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec))
    {
        if (it->path().filename().string().find(".tmp.") != std::string::npos)
        {
            Fail("recovery left staging path: " + it->path().string());
        }
    }
    if (ec)
    {
        Fail("failed to inspect recovered tree: " + ec.message());
    }
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        Fail("usage: fsx_journal_failpoint_tests CHILD_EXE TEST_ROOT");
    }

    const std::string child = argv[1];
    const fs::path test_root = argv[2];
    for (const std::string scenario :
         {"recover_replace_before_marker", "recover_replace_after_marker", "recover_replace_write_failure"})
    {
        const auto root = test_root / scenario;
        std::string child_error;
        const auto code = RunFailpointChild(child, scenario, root, &child_error);
        if (code != (scenario == "recover_replace_write_failure" ? 0 : 86) || ReadText(root / "target.txt") != "old" ||
            !fs::exists(root / "run.journal"))
            Fail("replacement recovery crash/failure did not preserve restored state: " + scenario);
        if (scenario == "recover_replace_after_marker")
        {
            // Model a crash part way through cleanup after the durable marker.
            for (const auto& entry : fs::directory_iterator(root))
                if (entry.path().filename().string().find(".tmp.") != std::string::npos)
                    fs::remove(entry.path());
        }
        const auto retried = fsx::RecoverFromJournal((root / "run.journal").string());
        if (!retried.ok || !retried.rollback_complete || ReadText(root / "target.txt") != "old")
            Fail("replacement recovery retry failed: " + scenario + ": " + retried.error);
        VerifyNoStagingFiles(root);
    }
    for (const std::string scenario : {"rollback_conflict_best_effort", "rollback_conflict_strict"})
    {
        const auto root = test_root / scenario;
        std::string child_error;
        const int child_exit = RunFailpointChild(child, scenario, root, &child_error);
        if (child_exit != 0)
            Fail("rollback contract child failed: " + scenario + ": " + child_error + ": " +
                 std::to_string(child_exit));
        if (ReadText(root / "source.txt") != "foreign" || ReadText(root / "destination.txt") != "source" ||
            !fs::exists(root / "run.journal") ||
            fs::exists(root / "independent.txt") != (scenario == "rollback_conflict_strict"))
            Fail("rollback conflict lost evidence or did not honor its effort policy");
        bool kept_old_backup = false;
        for (const auto& entry : fs::directory_iterator(root))
            if (entry.path().filename().string().find("destination.txt.old.tmp.") == 0 &&
                ReadText(entry.path()) == "old")
                kept_old_backup = true;
        if (!kept_old_backup)
            Fail("rollback conflict removed its dependent old-data backup");
    }
    const std::vector<Scenario> scenarios = {
        {"atomic_write", {{"target.txt", "old"}}},
        {"atomic_write_after", {{"target.txt", "old"}}},
        {"atomic_write_foreign", {{"target.txt", "foreign"}}},
        {"atomic_write_missing", {}},
        {"atomic_write_directory", {}},
        {"atomic_write_witness_corrupt", {{"target.txt", "new"}}},
        {"copy_overwrite", {{"source.txt", "source"}, {"destination.txt", "old"}}},
        {"copy_new_parent", {{"source.txt", "source"}}},
        {"copy_new_parent_after", {{"source.txt", "source"}}},
        {"copy_new_parent_nested", {{"source.txt", "source"}}},
        {"copy_new_parent_conflict", {{"source.txt", "source"}}},
        {"safe_replace", {{"source.txt", "source"}, {"destination.txt", "old"}}},
        {"rename", {{"source.txt", "source"}, {"destination.txt", "old"}}},
        {"remove", {{"target.txt", "old"}}},
        {"copy_tree",
         {{"source/nested/item.txt", "source"},
          {"destination/nested/item.txt", "old"},
          {"destination/keep.txt", "keep"}}},
    };

    for (const auto& scenario : scenarios)
    {
        const fs::path root = test_root / scenario.name;
        std::error_code ec;
        fs::remove_all(root, ec);

        std::string child_error;
        const int child_exit = RunFailpointChild(child, scenario.name, root, &child_error);
        if (child_exit < 0)
        {
            Fail("failed to launch failpoint child for " + scenario.name + ": " + child_error);
        }
        if (child_exit != 86)
        {
            Fail("failpoint child missed the requested crash point for " + scenario.name + ": " +
                 std::to_string(child_exit));
        }

        const fs::path journal = root / "run.journal";
        if (!fs::exists(journal, ec) || ec)
        {
            Fail("failpoint child did not leave a journal for " + scenario.name);
        }
        if (scenario.name.rfind("atomic_write", 0) == 0)
        {
            // The child is stopped at exact native publication boundaries;
            // these observations do not rely on probabilistic reader polling.
            const auto expected = scenario.name == "atomic_write" ? "old" : "new";
            if (!fs::is_regular_file(root / "target.txt") || ReadText(root / "target.txt") != expected)
                Fail("atomic publication boundary lost its readable old/new target");
        }
        if (scenario.name == "atomic_write_foreign" || scenario.name == "atomic_write_missing" ||
            scenario.name == "atomic_write_directory" || scenario.name == "atomic_write_witness_corrupt")
        {
            if (scenario.name == "atomic_write_foreign")
                std::ofstream(root / "target.txt", std::ios::binary | std::ios::trunc) << "foreign";
            else if (scenario.name == "atomic_write_missing" || scenario.name == "atomic_write_directory")
            {
                fs::remove(root / "target.txt");
                if (scenario.name == "atomic_write_directory")
                    fs::create_directory(root / "target.txt");
            }
            else
                for (const auto& entry : fs::directory_iterator(root))
                    if (entry.path().filename().string().find("target.txt.old.tmp.") == 0)
                        std::ofstream(entry.path(), std::ios::binary | std::ios::trunc) << "damaged old witness";
            const auto before = ReadText(journal);
            const auto conflict = fsx::RecoverFromJournal(journal.string());
            if (conflict.ok || conflict.rollback_complete || !fs::exists(journal) || ReadText(journal) != before)
                Fail("replacement recovery accepted a modified/missing target or witness");
            std::size_t witness_count = 0;
            for (const auto& entry : fs::directory_iterator(root))
                if (entry.path().filename().string().find(".tmp.") != std::string::npos)
                    ++witness_count;
            if (witness_count < 2)
                Fail("replacement recovery conflict removed witnesses");
            continue;
        }

        if (scenario.name == "copy_new_parent_conflict")
        {
            std::ofstream(root / "new-parent", std::ios::binary) << "foreign file";
            const auto conflict = fsx::RecoverFromJournal(journal.string());
            if (conflict.ok || !fs::exists(journal) || ReadText(root / "new-parent") != "foreign file")
                Fail("parent creation recovery failed to preserve conflict evidence");
            continue;
        }
        const auto recovered = fsx::RecoverFromJournal(journal.string());
        if (!recovered.ok)
        {
            Fail("recovery failed for " + scenario.name + ": " + recovered.error);
        }
        if (fs::exists(journal, ec) || ec)
        {
            Fail("recovery did not clean journal for " + scenario.name);
        }

        for (const auto& expected : scenario.expected_files)
        {
            const fs::path path = root / expected.relative_path;
            if (!fs::exists(path, ec) || ec)
            {
                Fail("recovery lost original file for " + scenario.name + ": " + path.string());
            }
            if (ReadText(path) != expected.text)
            {
                Fail("recovery changed original content for " + scenario.name + ": " + path.string());
            }
        }
        if (scenario.name.rfind("copy_new_parent", 0) == 0 && fs::exists(root / "new-parent"))
            Fail("recovery left newly created parents");
        VerifyNoStagingFiles(root);
    }

    std::error_code ec;
    fs::remove_all(test_root, ec);
    return 0;
}
