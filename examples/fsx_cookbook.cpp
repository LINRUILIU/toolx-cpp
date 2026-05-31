#include "fsx.h"

#include <filesystem>
#include <fstream>
#include <iostream>

namespace
{

void WriteText(const std::filesystem::path& path, const std::string& text)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << text;
}

std::string ReadText(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

} // namespace

int main()
{
    const auto root = std::filesystem::current_path() / "temp" / "toolx_fsx_cookbook";
    const auto src = root / "src";
    const auto dst = root / "dst";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);

    // Scenario 1: BatchPlan groups writes and replacements with rollback metadata.
    WriteText(src / "next.txt", "next");
    fsx::BatchPlan batch;
    batch.AddAtomicWrite((dst / "config.json").string(), R"({"ok":true})")
        .AddSafeReplace((src / "next.txt").string(), (dst / "current.txt").string(), true);
    const auto batch_result = fsx::Run(batch);
    std::cout << "batch-ok=" << batch_result.ok << " steps=" << batch_result.steps.size() << "\n";

    // Scenario 2: directory walking can return relative files and directories.
    WriteText(src / "nested" / "a.txt", "alpha");
    const auto walk = fsx::WalkDirectory(src.string());
    std::cout << "walk-ok=" << walk.ok << " entries=" << walk.entries.size() << "\n";

    // Scenario 3: diff/sync copies changed files and removes extra destination files.
    WriteText(dst / "old.txt", "remove-me");
    const auto diff = fsx::BuildDirectoryDiff(src.string(), dst.string(), true);
    auto sync_plan = fsx::BuildSyncPlan(src.string(), dst.string(), true);
    const auto sync_result = fsx::Run(sync_plan);
    std::cout << "diff=" << diff.entries.size() << " sync-ok=" << sync_result.ok << "\n";

    // Scenario 4: tar archive MVP round-trips regular files and directories.
    const auto archive = root / "bundle.tar";
    const auto unpacked = root / "unpacked";
    fsx::CreateArchive(src.string(), archive.string());
    fsx::ExtractArchive(archive.string(), unpacked.string());
    std::cout << "archive-text=" << ReadText(unpacked / "nested" / "a.txt") << "\n";

    // Scenario 5: capability flags keep feature decisions out of platform-specific code.
    const auto caps = fsx::QueryCapabilities();
    std::cout << "tar=" << caps.tar_archive << " zip=" << caps.zip_archive << "\n";
    std::filesystem::remove_all(root, ec);
    return 0;
}
