#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
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

void WriteText(const std::filesystem::path& p, const std::string& text)
{
    std::error_code ec;
    std::filesystem::create_directories(p.parent_path(), ec);
    std::ofstream out(p.string(), std::ios::binary);
    out << text;
}

std::string ReadText(const std::filesystem::path& p)
{
    std::ifstream in(p.string(), std::ios::binary);
    std::string s((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return s;
}

void WriteOctal(char* field, std::size_t width, std::uintmax_t value)
{
    std::snprintf(field, width, "%0*llo", static_cast<int>(width - 1), static_cast<unsigned long long>(value));
}

void WriteTarWithSingleEntry(const std::filesystem::path& archive, const std::string& name, const std::string& payload)
{
    std::array<char, 512> header{};
    std::memcpy(header.data(), name.data(), std::min<std::size_t>(name.size(), 100));
    WriteOctal(header.data() + 100, 8, 0644);
    WriteOctal(header.data() + 108, 8, 0);
    WriteOctal(header.data() + 116, 8, 0);
    WriteOctal(header.data() + 124, 12, payload.size());
    WriteOctal(header.data() + 136, 12, 0);
    std::memset(header.data() + 148, ' ', 8);
    header[156] = '0';
    std::memcpy(header.data() + 257, "ustar", 5);
    std::memcpy(header.data() + 263, "00", 2);

    unsigned int checksum = 0;
    for (const char ch : header)
    {
        checksum += static_cast<unsigned char>(ch);
    }
    std::snprintf(header.data() + 148, 8, "%06o", checksum);
    header[154] = '\0';
    header[155] = ' ';

    std::ofstream out(archive, std::ios::binary | std::ios::trunc);
    out.write(header.data(), static_cast<std::streamsize>(header.size()));
    out.write(payload.data(), static_cast<std::streamsize>(payload.size()));
    const std::size_t padding = (512 - (payload.size() % 512)) % 512;
    std::array<char, 512> zeros{};
    if (padding > 0)
    {
        out.write(zeros.data(), static_cast<std::streamsize>(padding));
    }
    out.write(zeros.data(), static_cast<std::streamsize>(zeros.size()));
    out.write(zeros.data(), static_cast<std::streamsize>(zeros.size()));
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
    options.fail_fast = false;
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

TEST(FsxTests, RecoverFromJournalRestoresAllUndoActionsForOverwrittenCopy)
{
    const auto root = TestRoot() / "recover_copy_overwrite";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);

    const auto source = root / "source.txt";
    const auto destination = root / "destination.txt";
    const auto journal = root / "run.journal";
    WriteText(source, "new");
    WriteText(destination, "old");

    fsx::BatchPlan plan;
    plan.AddCopyFile(source.string(), destination.string())
        .AddSafeReplace((root / "missing.txt").string(), (root / "later.txt").string(), false);

    fsx::RunOptions options;
    options.fail_fast = false;
    options.journal_path = journal.string();

    const auto run_result = fsx::Run(plan, options);
    ASSERT_FALSE(run_result.ok);
    EXPECT_EQ(ReadText(destination), "new");
    EXPECT_TRUE(std::filesystem::exists(journal));

    const auto recovered = fsx::RecoverFromJournal(journal.string());
    ASSERT_TRUE(recovered.ok) << recovered.error;
    EXPECT_EQ(ReadText(source), "new");
    EXPECT_EQ(ReadText(destination), "old");
    EXPECT_FALSE(std::filesystem::exists(journal));

    std::filesystem::remove_all(root, ec);
}

TEST(FsxTests, CommittedJournalCannotBeRecoveredAndCanBeReused)
{
    const auto root = TestRoot() / "committed_journal";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);

    const auto target = root / "target.txt";
    const auto journal = root / "run.journal";
    std::filesystem::create_directories(root, ec);
    fsx::RunOptions options;
    options.journal_path = journal.string();
    options.keep_journal_on_success = true;

    fsx::BatchPlan first;
    first.AddAtomicWrite(target.string(), "first");
    ASSERT_TRUE(fsx::Run(first, options).ok);
    EXPECT_TRUE(std::filesystem::exists(journal));

    const auto recovered = fsx::RecoverFromJournal(journal.string());
    EXPECT_FALSE(recovered.ok);
    EXPECT_NE(recovered.error.find("completed transaction"), std::string::npos);
    EXPECT_EQ(ReadText(target), "first");

    fsx::BatchPlan second;
    second.AddAtomicWrite(target.string(), "second");
    const auto rerun = fsx::Run(second, options);
    ASSERT_TRUE(rerun.ok) << rerun.error;
    EXPECT_EQ(ReadText(target), "second");

    std::filesystem::remove_all(root, ec);
}

TEST(FsxTests, FailFastRollbackRestoresExistingAtomicWriteTarget)
{
    const auto root = TestRoot() / "rollback_existing_atomic";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);

    const auto target = root / "target.txt";
    WriteText(target, "old");

    fsx::BatchPlan plan;
    plan.AddAtomicWrite(target.string(), "new")
        .AddSafeReplace((root / "missing.txt").string(), (root / "dest.txt").string(), false);

    const auto result = fsx::Run(plan);
    ASSERT_FALSE(result.ok);
    EXPECT_EQ(ReadText(target), "old");

    std::filesystem::remove_all(root, ec);
}

TEST(FsxTests, FailFastRollbackRemovesJournalAfterSuccessfulRollback)
{
    const auto root = TestRoot() / "rollback_journal_cleanup";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);

    const auto target = root / "target.txt";
    const auto journal = root / "run.journal";
    fsx::BatchPlan plan;
    plan.AddAtomicWrite(target.string(), "new")
        .AddSafeReplace((root / "missing.txt").string(), (root / "later.txt").string(), false);

    fsx::RunOptions options;
    options.journal_path = journal.string();
    const auto result = fsx::Run(plan, options);
    ASSERT_FALSE(result.ok);
    EXPECT_FALSE(std::filesystem::exists(target));
    EXPECT_FALSE(std::filesystem::exists(journal));

    std::filesystem::remove_all(root, ec);
}

TEST(FsxTests, FailFastRollbackRestoresSafeReplaceSourceAndDestination)
{
    const auto root = TestRoot() / "rollback_safe_replace";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);

    const auto src = root / "src.txt";
    const auto dst = root / "dst.txt";
    WriteText(src, "new");
    WriteText(dst, "old");

    fsx::BatchPlan plan;
    plan.AddSafeReplace(src.string(), dst.string(), false)
        .AddSafeReplace((root / "missing.txt").string(), (root / "later.txt").string(), false);

    const auto result = fsx::Run(plan);
    ASSERT_FALSE(result.ok);
    EXPECT_EQ(ReadText(src), "new");
    EXPECT_EQ(ReadText(dst), "old");

    std::filesystem::remove_all(root, ec);
}

TEST(FsxTests, NonFailFastFailureReturnsFailureAndKeepsJournal)
{
    const auto root = TestRoot() / "non_fail_fast";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);

    const auto created = root / "created.txt";
    const auto journal = root / "run.journal";
    std::filesystem::create_directories(root, ec);
    fsx::BatchPlan plan;
    plan.AddAtomicWrite(created.string(), "created")
        .AddSafeReplace((root / "missing.txt").string(), (root / "dest.txt").string(), false);

    fsx::RunOptions options;
    options.fail_fast = false;
    options.journal_path = journal.string();

    const auto result = fsx::Run(plan, options);
    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.error.empty());
    EXPECT_TRUE(std::filesystem::exists(created));
    EXPECT_TRUE(std::filesystem::exists(journal));

    std::filesystem::remove_all(root, ec);
}

TEST(FsxTests, RunFailsWhenJournalCannotBeCreated)
{
    const auto root = TestRoot() / "journal_open_failure";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);

    fsx::BatchPlan plan;
    plan.AddAtomicWrite((root / "out.txt").string(), "payload");
    fsx::RunOptions options;
    options.journal_path = (root / "missing" / "run.journal").string();
    options.keep_journal_on_success = true;

    const auto result = fsx::Run(plan, options);
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error.find("journal"), std::string::npos);
    EXPECT_FALSE(std::filesystem::exists(root / "out.txt"));

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
    for (const auto& entry : walked.entries)
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

TEST(FsxTests, ArchiveCapabilitiesExposeTarMvp)
{
    const auto caps = fsx::QueryCapabilities();
    EXPECT_FALSE(caps.zip_archive);
    EXPECT_TRUE(caps.tar_archive);
}

TEST(FsxTests, DirectoryDiffBuildsSyncPlan)
{
    const auto root = TestRoot() / "sync_plan";
    const auto src = root / "src";
    const auto dst = root / "dst";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);

    WriteText(src / "a.txt", "new");
    WriteText(src / "nested" / "b.txt", "b");
    WriteText(dst / "a.txt", "old");
    WriteText(dst / "remove.txt", "remove");

    const auto diff = fsx::BuildDirectoryDiff(src.string(), dst.string(), true);
    ASSERT_TRUE(diff.ok) << diff.error;
    ASSERT_EQ(diff.entries.size(), 3u);

    auto plan = fsx::BuildSyncPlan(src.string(), dst.string(), true);
    fsx::RunOptions options;
    options.conflict_policy = fsx::ConflictPolicy::Overwrite;
    const auto run = fsx::Run(plan, options);
    ASSERT_TRUE(run.ok) << run.error;
    EXPECT_EQ(ReadText(dst / "a.txt"), "new");
    EXPECT_EQ(ReadText(dst / "nested" / "b.txt"), "b");
    EXPECT_FALSE(std::filesystem::exists(dst / "remove.txt"));
}

TEST(FsxTests, BuildSyncPlanReportsInvalidSourceRoot)
{
    const auto root = TestRoot() / "sync_plan_invalid_source";
    const auto missing_src = root / "missing";
    const auto dst = root / "dst";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(dst, ec);

    const auto plan = fsx::BuildSyncPlan(missing_src.string(), dst.string(), true);
    EXPECT_FALSE(plan.ok());
    EXPECT_TRUE(plan.Actions().empty());
    EXPECT_NE(plan.error().find("root does not exist"), std::string::npos);

    const auto run = fsx::Run(plan);
    EXPECT_FALSE(run.ok);
    EXPECT_EQ(run.error, plan.error());

    std::filesystem::remove_all(root, ec);
}

TEST(FsxTests, BuildSyncPlanRejectsSameOrNestedRoots)
{
    const auto root = TestRoot() / "sync_plan_boundaries";
    const auto src = root / "src";
    const auto nested_dst = src / "dst";
    const auto parent_dst = root / "parent_dst";
    const auto nested_src = parent_dst / "src";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);

    WriteText(src / "a.txt", "a");
    WriteText(nested_dst / "extra.txt", "extra");
    WriteText(nested_src / "b.txt", "b");

    const auto same = fsx::BuildSyncPlan(src.string(), src.string(), true);
    EXPECT_FALSE(same.ok());
    EXPECT_TRUE(same.Actions().empty());
    EXPECT_NE(same.error().find("different"), std::string::npos);

    const auto dst_inside_src = fsx::BuildSyncPlan(src.string(), nested_dst.string(), true);
    EXPECT_FALSE(dst_inside_src.ok());
    EXPECT_TRUE(dst_inside_src.Actions().empty());
    EXPECT_NE(dst_inside_src.error().find("overlap"), std::string::npos);

    const auto src_inside_dst = fsx::BuildSyncPlan(nested_src.string(), parent_dst.string(), true);
    EXPECT_FALSE(src_inside_dst.ok());
    EXPECT_TRUE(src_inside_dst.Actions().empty());
    EXPECT_NE(src_inside_dst.error().find("overlap"), std::string::npos);

    std::filesystem::remove_all(root, ec);
}

TEST(FsxTests, TarArchiveCreateAndExtractRoundTrip)
{
    const auto root = TestRoot() / "tar_archive";
    const auto src = root / "src";
    const auto out = root / "out";
    const auto archive = root / "bundle.tar";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);

    WriteText(src / "a.txt", "alpha");
    WriteText(src / "nested" / "b.txt", "beta");

    ASSERT_TRUE(fsx::CreateArchive(src.string(), archive.string()).ok);
    ASSERT_TRUE(fsx::ExtractArchive(archive.string(), out.string()).ok);

    EXPECT_EQ(ReadText(out / "a.txt"), "alpha");
    EXPECT_EQ(ReadText(out / "nested" / "b.txt"), "beta");
}

TEST(FsxTests, ExtractArchiveRejectsTraversalEntry)
{
    const auto root = TestRoot() / "tar_traversal";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
    const auto archive = root / "evil.tar";
    const auto out = root / "out";

    WriteTarWithSingleEntry(archive, "../evil.txt", "owned");
    const auto status = fsx::ExtractArchive(archive.string(), out.string());
    EXPECT_FALSE(status.ok);
    EXPECT_NE(status.error.find("unsafe archive entry"), std::string::npos);
    EXPECT_FALSE(std::filesystem::exists(root / "evil.txt"));

    std::filesystem::remove_all(root, ec);
}

TEST(FsxTests, ExtractArchiveRejectsWindowsRootedEntry)
{
    const auto root = TestRoot() / "tar_windows_root";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
    const auto archive = root / "evil.tar";
    const auto out = root / "out";

    WriteTarWithSingleEntry(archive, "C:/evil.txt", "owned");
    const auto drive_status = fsx::ExtractArchive(archive.string(), out.string());
    EXPECT_FALSE(drive_status.ok);
    EXPECT_NE(drive_status.error.find("unsafe archive entry"), std::string::npos);

    WriteTarWithSingleEntry(archive, "\\\\evil.txt", "owned");
    const auto slash_status = fsx::ExtractArchive(archive.string(), out.string());
    EXPECT_FALSE(slash_status.ok);
    EXPECT_NE(slash_status.error.find("unsafe archive entry"), std::string::npos);
    EXPECT_FALSE(std::filesystem::exists(root / "evil.txt"));

    std::filesystem::remove_all(root, ec);
}

#if !defined(_WIN32)
TEST(FsxTests, ExtractArchiveRejectsPreexistingSymlinkEscape)
{
    const auto root = TestRoot() / "tar_symlink_escape";
    const auto archive = root / "evil.tar";
    const auto out = root / "out";
    const auto outside = root / "outside";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(out, ec);
    std::filesystem::create_directories(outside, ec);
    std::filesystem::create_directory_symlink(outside, out / "link", ec);
    if (ec)
    {
        GTEST_SKIP() << "directory symlinks are unavailable: " << ec.message();
    }

    WriteTarWithSingleEntry(archive, "link/escape.txt", "owned");
    const auto status = fsx::ExtractArchive(archive.string(), out.string());
    EXPECT_FALSE(status.ok);
    EXPECT_NE(status.error.find("escapes destination root"), std::string::npos);
    EXPECT_FALSE(std::filesystem::exists(outside / "escape.txt"));

    std::filesystem::remove_all(root, ec);
}
#endif
