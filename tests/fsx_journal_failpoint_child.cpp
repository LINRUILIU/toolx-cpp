#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "fsx.h"

namespace
{
namespace fs = std::filesystem;

void WriteText(const fs::path& path, const std::string& text)
{
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}

bool SetFailpoint(const std::string& hit)
{
#if defined(_WIN32)
    return _putenv_s("TOOLX_FSX_TEST_FAILPOINT_HIT", hit.c_str()) == 0 &&
           _putenv_s("TOOLX_FSX_TEST_FAILPOINT", "after-journal-sync-before-mutation") == 0;
#else
    return setenv("TOOLX_FSX_TEST_FAILPOINT_HIT", hit.c_str(), 1) == 0 &&
           setenv("TOOLX_FSX_TEST_FAILPOINT", "after-journal-sync-before-mutation", 1) == 0;
#endif
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        std::cerr << "usage: fsx_journal_failpoint_child SCENARIO ROOT\n";
        return 2;
    }

    const std::string scenario = argv[1];
    const fs::path root = argv[2];
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);

    fsx::BatchPlan plan;
    if (scenario == "atomic_write")
    {
        const fs::path target = root / "target.txt";
        WriteText(target, "old");
        plan.AddAtomicWrite(target.string(), "new");
    }
    else if (scenario == "copy_overwrite")
    {
        const fs::path source = root / "source.txt";
        const fs::path destination = root / "destination.txt";
        WriteText(source, "source");
        WriteText(destination, "old");
        plan.AddCopyFile(source.string(), destination.string());
    }
    else if (scenario.rfind("copy_new_parent", 0) == 0)
    {
        WriteText(root / "source.txt", "source");
        plan.AddCopyFile((root / "source.txt").string(), (root / "new-parent" / "nested" / "item.txt").string());
    }
    else if (scenario == "safe_replace")
    {
        const fs::path source = root / "source.txt";
        const fs::path destination = root / "destination.txt";
        WriteText(source, "source");
        WriteText(destination, "old");
        plan.AddSafeReplace(source.string(), destination.string(), false);
    }
    else if (scenario == "rename")
    {
        const fs::path source = root / "source.txt";
        const fs::path destination = root / "destination.txt";
        WriteText(source, "source");
        WriteText(destination, "old");
        plan.AddRename(source.string(), destination.string());
    }
    else if (scenario == "remove")
    {
        const fs::path target = root / "target.txt";
        WriteText(target, "old");
        plan.AddRemovePath(target.string());
    }
    else if (scenario == "copy_tree")
    {
        const fs::path source = root / "source";
        const fs::path destination = root / "destination";
        WriteText(source / "nested" / "item.txt", "source");
        WriteText(destination / "nested" / "item.txt", "old");
        WriteText(destination / "keep.txt", "keep");
        plan.AddCopyTree(source.string(), destination.string());
    }
    else
    {
        std::cerr << "unknown scenario: " << scenario << "\n";
        return 2;
    }

    const std::string hit = scenario == "copy_new_parent_after"      ? "3"
                            : scenario == "copy_new_parent_nested"   ? "5"
                            : scenario == "copy_new_parent_conflict" ? "2"
                                                                     : "1";
    if (!SetFailpoint(hit))
    {
        std::cerr << "failed to set failpoint\n";
        return 2;
    }

    fsx::RunOptions options;
    options.conflict_policy = fsx::ConflictPolicy::Overwrite;
    options.journal_path = (root / "run.journal").string();
    options.keep_journal_on_success = true;
    const auto result = fsx::Run(plan, options);

    std::cerr << "failpoint did not terminate process: " << result.error << "\n";
    return result.ok ? 3 : 4;
}
