#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#include "fsx.h"

namespace
{

    std::filesystem::path TestRoot()
    {
        return std::filesystem::current_path() / "toolx_test_tmp" / "fsx_tests_root";
    }

    void WriteText(const std::filesystem::path &p, const std::string &text)
    {
        std::error_code ec;
        std::filesystem::create_directories(p.parent_path(), ec);
        std::ofstream out(p.string(), std::ios::binary);
        out << text;
    }

    std::string ReadText(const std::filesystem::path &p)
    {
        std::ifstream in(p.string(), std::ios::binary);
        std::string s((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        return s;
    }

} // namespace

TEST(FsxTests, AtomicWriteCreatesFile)
{
    const auto root = TestRoot() / "atomic_write";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);

    fsx::BatchPlan plan;
    const auto target = root / "a.txt";
    plan.AddAtomicWrite(target.string(), "v1");

    const auto result = fsx::Run(plan);
    ASSERT_TRUE(result.ok);
    EXPECT_TRUE(std::filesystem::exists(target));
    EXPECT_EQ(ReadText(target), "v1");

    std::filesystem::remove_all(root, ec);
}

TEST(FsxTests, SafeReplaceDefaultOverwrite)
{
    const auto root = TestRoot() / "safe_replace";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);

    const auto src = root / "src.txt";
    const auto dst = root / "dst.txt";
    WriteText(src, "new");
    WriteText(dst, "old");

    fsx::BatchPlan plan;
    plan.AddSafeReplace(src.string(), dst.string(), false);

    const auto result = fsx::Run(plan);
    ASSERT_TRUE(result.ok);
    EXPECT_FALSE(std::filesystem::exists(src));
    EXPECT_TRUE(std::filesystem::exists(dst));
    EXPECT_EQ(ReadText(dst), "new");

    std::filesystem::remove_all(root, ec);
}

TEST(FsxTests, FailFastRollback)
{
    const auto root = TestRoot() / "rollback";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);

    const auto created = root / "created.txt";

    fsx::BatchPlan plan;
    plan.AddAtomicWrite(created.string(), "temp")
        .AddSafeReplace((root / "missing.txt").string(), (root / "dest.txt").string(), false);

    fsx::RunOptions options;
    options.fail_fast = true;

    const auto result = fsx::Run(plan, options);
    ASSERT_FALSE(result.ok);
    EXPECT_FALSE(std::filesystem::exists(created));
    ASSERT_EQ(result.steps.size(), 2U);
    EXPECT_TRUE(result.steps[0].ok);
    EXPECT_TRUE(result.steps[0].rolled_back);
    EXPECT_FALSE(result.steps[1].ok);

    std::filesystem::remove_all(root, ec);
}

TEST(FsxTests, SafeReplaceBackupCreatesBackupFile)
{
    const auto root = TestRoot() / "backup";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);

    const auto src = root / "src.txt";
    const auto dst = root / "dst.txt";
    WriteText(src, "new-data");
    WriteText(dst, "old-data");

    fsx::BatchPlan plan;
    plan.AddSafeReplace(src.string(), dst.string(), true);

    fsx::RunOptions options;
    options.backup_suffix = ".keep";

    const auto result = fsx::Run(plan, options);
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(ReadText(dst), "new-data");

    const auto backup = std::filesystem::path(dst.string() + ".keep");
    EXPECT_TRUE(std::filesystem::exists(backup));
    EXPECT_EQ(ReadText(backup), "old-data");

    std::filesystem::remove_all(root, ec);
}

TEST(FsxTests, ConflictPolicySkipMarksSkipped)
{
    const auto root = TestRoot() / "conflict_skip";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);

    const auto src = root / "src.txt";
    const auto dst = root / "dst.txt";
    WriteText(src, "source");
    WriteText(dst, "dest");

    fsx::BatchPlan plan;
    plan.AddRename(src.string(), dst.string());

    fsx::RunOptions options;
    options.conflict_policy = fsx::ConflictPolicy::Skip;

    const auto result = fsx::Run(plan, options);
    ASSERT_TRUE(result.ok);
    ASSERT_EQ(result.steps.size(), 1U);
    EXPECT_TRUE(result.steps[0].ok);
    EXPECT_TRUE(result.steps[0].skipped);
    EXPECT_EQ(result.skipped_steps, 1U);
    EXPECT_TRUE(std::filesystem::exists(src));
    EXPECT_EQ(ReadText(dst), "dest");

    std::filesystem::remove_all(root, ec);
}

TEST(FsxTests, RecoverFromJournalRollsBack)
{
    const auto root = TestRoot() / "recover";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);

    const auto target = root / "a.txt";
    const auto src = root / "b.txt";
    const auto dst = root / "c.txt";
    const auto journal = root / "run.journal";

    WriteText(src, "B");

    fsx::BatchPlan plan;
    plan.AddAtomicWrite(target.string(), "A")
        .AddRename(src.string(), dst.string())
        .AddSafeReplace((root / "missing.txt").string(), (root / "x.txt").string(), false);

    fsx::RunOptions options;
    options.journal_path = journal.string();
    options.keep_journal_on_success = true;

    const auto run_result = fsx::Run(plan, options);
    ASSERT_FALSE(run_result.ok);
    EXPECT_TRUE(std::filesystem::exists(journal));

    fsx::RecoverOptions recover_options;
    recover_options.cleanup_journal_on_success = true;

    const auto recover_result = fsx::RecoverFromJournal(journal.string(), recover_options);
    ASSERT_TRUE(recover_result.ok);
    EXPECT_FALSE(std::filesystem::exists(target));
    EXPECT_TRUE(std::filesystem::exists(src));
    EXPECT_FALSE(std::filesystem::exists(dst));
    EXPECT_FALSE(std::filesystem::exists(journal));

    std::filesystem::remove_all(root, ec);
}

TEST(FsxTests, WalkDirectoryRecursiveCollectsEntries)
{
    const auto root = TestRoot() / "walk";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);

    WriteText(root / "a.txt", "a");
    WriteText(root / "nested" / "b.txt", "bb");

    fsx::WalkOptions options;
    options.recursive = true;
    options.relative_path = true;
    const auto walked = fsx::WalkDirectory(root.string(), options);
    ASSERT_TRUE(walked.ok) << walked.error;

    bool found_a = false;
    bool found_b = false;
    for (const auto &entry : walked.entries)
    {
        if (entry.path == "a.txt")
        {
            found_a = true;
        }
        if (entry.path == "nested/b.txt")
        {
            found_b = true;
            EXPECT_EQ(entry.size, 2u);
        }
    }
    EXPECT_TRUE(found_a);
    EXPECT_TRUE(found_b);

    std::filesystem::remove_all(root, ec);
}

TEST(FsxTests, PollingFileWatcherDetectsModification)
{
    const auto root = TestRoot() / "watch";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);

    const auto file = root / "watch.txt";
    WriteText(file, "v1");

    auto watcher = fsx::CreateFileWatcher(file.string());
    ASSERT_NE(watcher, nullptr);

    const auto first = watcher->Poll(0);
    ASSERT_TRUE(first.ok) << first.error;
    EXPECT_FALSE(first.has_event);

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    WriteText(file, "v2");

    const auto changed = watcher->Poll(200);
    ASSERT_TRUE(changed.ok) << changed.error;
    EXPECT_TRUE(changed.has_event);
    EXPECT_EQ(changed.event.kind, fsx::WatchEventKind::Modified);

    std::filesystem::remove_all(root, ec);
}

TEST(FsxTests, DirectoryWatcherDetectsCreateAndRemove)
{
    const auto root = TestRoot() / "watch_dir";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);

    auto watcher = fsx::CreateFileWatcher(root.string());
    ASSERT_NE(watcher, nullptr);

    const auto first = watcher->Poll(0);
    ASSERT_TRUE(first.ok) << first.error;
    EXPECT_FALSE(first.has_event);

    const auto file = root / "new.txt";
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    WriteText(file, "hello");

    const auto created = watcher->Poll(300);
    ASSERT_TRUE(created.ok) << created.error;
    ASSERT_TRUE(created.has_event);
    EXPECT_EQ(created.event.kind, fsx::WatchEventKind::Created);
    EXPECT_EQ(created.event.path, "new.txt");

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    std::filesystem::remove(file, ec);

    const auto removed = watcher->Poll(300);
    ASSERT_TRUE(removed.ok) << removed.error;
    ASSERT_TRUE(removed.has_event);
    EXPECT_EQ(removed.event.kind, fsx::WatchEventKind::Removed);
    EXPECT_EQ(removed.event.path, "new.txt");

    std::filesystem::remove_all(root, ec);
}

TEST(FsxTests, PollingFileWatcherConsumesEventOnlyOnce)
{
    const auto root = TestRoot() / "watch_once";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);

    const auto file = root / "watch.txt";
    WriteText(file, "v1");

    auto watcher = fsx::CreateFileWatcher(file.string());
    ASSERT_NE(watcher, nullptr);

    const auto first = watcher->Poll(0);
    ASSERT_TRUE(first.ok) << first.error;
    EXPECT_FALSE(first.has_event);

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    WriteText(file, "v2");

    const auto changed = watcher->Poll(200);
    ASSERT_TRUE(changed.ok) << changed.error;
    ASSERT_TRUE(changed.has_event);
    EXPECT_EQ(changed.event.kind, fsx::WatchEventKind::Modified);

    const auto idle = watcher->Poll(0);
    ASSERT_TRUE(idle.ok) << idle.error;
    EXPECT_FALSE(idle.has_event);

    std::filesystem::remove_all(root, ec);
}

TEST(FsxTests, CreateHardLinkWorksWhenSupported)
{
    const auto root = TestRoot() / "link";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);

    const auto caps = fsx::QueryCapabilities();
    if (!caps.hard_link)
    {
        GTEST_SKIP() << "hard link not supported";
    }

    const auto src = root / "src.txt";
    const auto link = root / "linked.txt";
    WriteText(src, "payload");

    const auto created = fsx::CreateLink(src.string(), link.string(), fsx::LinkType::Hard, false);
    ASSERT_TRUE(created.ok) << created.error;
    EXPECT_TRUE(std::filesystem::exists(link));
    EXPECT_EQ(ReadText(link), "payload");

    std::filesystem::remove_all(root, ec);
}

TEST(FsxTests, CreateLinkFailsWhenDestinationExistsWithoutOverwrite)
{
    const auto root = TestRoot() / "link_conflict";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);

    const auto caps = fsx::QueryCapabilities();
    if (!caps.hard_link)
    {
        GTEST_SKIP() << "hard link not supported";
    }

    const auto src = root / "src.txt";
    const auto link = root / "linked.txt";
    WriteText(src, "payload");
    WriteText(link, "existing");

    const auto created = fsx::CreateLink(src.string(), link.string(), fsx::LinkType::Hard, false);
    EXPECT_FALSE(created.ok);
    EXPECT_NE(created.error.find("already exists"), std::string::npos);

    std::filesystem::remove_all(root, ec);
}

TEST(FsxTests, ArchiveCapabilitiesAreFutureWork)
{
    const auto caps = fsx::QueryCapabilities();
    EXPECT_FALSE(caps.zip_archive);
    EXPECT_FALSE(caps.tar_archive);
}
