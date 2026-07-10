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
    return code;
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
    const std::vector<Scenario> scenarios = {
        {"atomic_write", {{"target.txt", "old"}}},
        {"copy_overwrite", {{"source.txt", "source"}, {"destination.txt", "old"}}},
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
        if (child_exit == 0)
        {
            Fail("failpoint child unexpectedly succeeded for " + scenario.name);
        }

        const fs::path journal = root / "run.journal";
        if (!fs::exists(journal, ec) || ec)
        {
            Fail("failpoint child did not leave a journal for " + scenario.name);
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
        VerifyNoStagingFiles(root);
    }

    std::error_code ec;
    fs::remove_all(test_root, ec);
    return 0;
}
