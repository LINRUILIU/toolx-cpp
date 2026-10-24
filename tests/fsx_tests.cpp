#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <locale>
#include <map>
#include <string>
#include <thread>

#include "fsx.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

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

class ScopedWorkingDirectory
{
  public:
    explicit ScopedWorkingDirectory(const std::filesystem::path& path) : original_(std::filesystem::current_path())
    {
        std::filesystem::current_path(path);
    }
    ~ScopedWorkingDirectory()
    {
        std::error_code ec;
        std::filesystem::current_path(original_, ec);
    }

  private:
    std::filesystem::path original_;
};

class GroupedNumbers : public std::numpunct<char>
{
  protected:
    char do_thousands_sep() const override
    {
        return ',';
    }
    std::string do_grouping() const override
    {
        return "\3";
    }
};

class ScopedGlobalLocale
{
  public:
    explicit ScopedGlobalLocale(const std::locale& locale) : original_(std::locale::global(locale)) {}
    ~ScopedGlobalLocale()
    {
        std::locale::global(original_);
    }

  private:
    std::locale original_;
};

#if defined(_WIN32)
// Directory enumeration alone misses ADS bytes. Preserve every ordinary file,
// every named/default stream, and all directory entries in rejection cases.
std::map<std::string, std::string> JournalBoundaryTreeState(const std::filesystem::path& root)
{
    std::map<std::string, std::string> state;
    state.emplace("directory:.", "");
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root))
    {
        const auto relative = entry.path().lexically_relative(root).generic_string();
        if (entry.is_directory())
        {
            state.emplace("directory:" + relative, "");
            continue;
        }
        EXPECT_TRUE(entry.is_regular_file());
        state.emplace("file:" + relative, ReadText(entry.path()));
        WIN32_FIND_STREAM_DATA stream{};
        const HANDLE handle = FindFirstStreamW(entry.path().c_str(), FindStreamInfoStandard, &stream, 0);
        EXPECT_NE(handle, INVALID_HANDLE_VALUE) << "FindFirstStreamW: " << GetLastError();
        if (handle == INVALID_HANDLE_VALUE)
            continue;
        do
        {
            const std::filesystem::path stream_name(stream.cStreamName);
            const std::filesystem::path stream_path(entry.path().native() + stream_name.native());
            state.emplace("stream:" + relative + stream_name.string(), ReadText(stream_path));
        } while (FindNextStreamW(handle, &stream));
        EXPECT_EQ(GetLastError(), static_cast<DWORD>(ERROR_HANDLE_EOF));
        EXPECT_TRUE(FindClose(handle));
    }
    return state;
}

std::filesystem::path ExtendedDrivePath(const std::filesystem::path& path)
{
    auto absolute = std::filesystem::absolute(path);
    absolute.make_preferred();
    return std::filesystem::path(L"\\\\?\\" + absolute.native());
}

void ExpectJournalStreamRejectedWithoutMutation(const fsx::BatchPlan& plan, const fsx::RunOptions& options,
                                                const std::filesystem::path& root)
{
    const auto before = JournalBoundaryTreeState(root);
    const auto result = fsx::Run(plan, options);
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error.find("stream"), std::string::npos) << result.error;
    EXPECT_EQ(result.completed_steps, 0U);
    EXPECT_TRUE(result.steps.empty());
    EXPECT_FALSE(result.rollback_attempted);
    EXPECT_EQ(JournalBoundaryTreeState(root), before);
}
#endif

void WriteOctal(char* field, std::size_t width, std::uintmax_t value)
{
    std::snprintf(field, width, "%0*llo", static_cast<int>(width - 1), static_cast<unsigned long long>(value));
}

void WriteTarEntry(std::ostream& out, const std::string& name, const std::string& payload, bool directory = false)
{
    std::array<char, 512> header{};
    std::memcpy(header.data(), name.data(), std::min<std::size_t>(name.size(), 100));
    WriteOctal(header.data() + 100, 8, 0644);
    WriteOctal(header.data() + 108, 8, 0);
    WriteOctal(header.data() + 116, 8, 0);
    WriteOctal(header.data() + 124, 12, payload.size());
    WriteOctal(header.data() + 136, 12, 0);
    std::memset(header.data() + 148, ' ', 8);
    header[156] = directory ? '5' : '0';
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

    out.write(header.data(), static_cast<std::streamsize>(header.size()));
    out.write(payload.data(), static_cast<std::streamsize>(payload.size()));
    const std::size_t padding = (512 - (payload.size() % 512)) % 512;
    std::array<char, 512> zeros{};
    if (padding > 0)
    {
        out.write(zeros.data(), static_cast<std::streamsize>(padding));
    }
}

void WriteTarWithSingleEntry(const std::filesystem::path& archive, const std::string& name, const std::string& payload)
{
    std::ofstream out(archive, std::ios::binary | std::ios::trunc);
    WriteTarEntry(out, name, payload);
    std::array<char, 1024> zeros{};
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

TEST(FsxTests, AtomicOverwriteKeepsExistingReaderAndPublishesNewContent)
{
    const auto root = TestRoot() / "atomic_reader";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    const auto target = root / "target.txt";
    WriteText(target, "old");
#if defined(_WIN32)
    HANDLE reader = CreateFileW(target.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    ASSERT_NE(reader, INVALID_HANDLE_VALUE);
#else
    std::ifstream reader(target, std::ios::binary);
    ASSERT_TRUE(reader.is_open());
#endif
    fsx::BatchPlan plan;
    plan.AddAtomicWrite(target.string(), "new");
    const auto result = fsx::Run(plan);
    EXPECT_TRUE(result.ok) << result.error;
    EXPECT_EQ(ReadText(target), "new");
#if defined(_WIN32)
    char buffer[3]{};
    DWORD count = 0;
    EXPECT_TRUE(ReadFile(reader, buffer, sizeof(buffer), &count, nullptr));
    EXPECT_EQ(std::string(buffer, count), "old");
    CloseHandle(reader);
#else
    EXPECT_EQ(std::string((std::istreambuf_iterator<char>(reader)), std::istreambuf_iterator<char>()), "old");
    reader.close();
#endif
    std::filesystem::remove_all(root, ec);
}

#if defined(_WIN32)
TEST(FsxTests, AtomicOverwriteBlockedByReaderPreservesOldTarget)
{
    const auto root = TestRoot() / "atomic_blocked_reader";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    const auto target = root / "target.txt";
    WriteText(target, "old");
    HANDLE reader = CreateFileW(target.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    ASSERT_NE(reader, INVALID_HANDLE_VALUE);
    fsx::BatchPlan plan;
    plan.AddAtomicWrite(target.string(), "new");
    fsx::RunOptions options;
    options.journal_path = (root / "run.journal").string();
    const auto result = fsx::Run(plan, options);
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.rollback_complete) << result.rollback_error;
    EXPECT_EQ(ReadText(target), "old");
    EXPECT_FALSE(std::filesystem::exists(options.journal_path));
    CloseHandle(reader);
    std::filesystem::remove_all(root, ec);
}
#endif

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
    EXPECT_TRUE(result.rollback_attempted);
    EXPECT_TRUE(result.rollback_complete);
    EXPECT_EQ(result.rolled_back_steps, 1U);
    EXPECT_FALSE(result.steps[1].rollback_attempted);

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

TEST(FsxTests, SafeReplaceRejectsRetainedBackupSourceAliases)
{
    const auto root = TestRoot() / "backup_source_alias";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    for (const bool default_backup : {false, true})
    {
        for (const bool lexical_alias : {false, true})
        {
            const auto dst = root / "target";
            const auto backup = root / "target.bak";
            const auto src = lexical_alias ? root / "." / "target.bak" : backup;
            WriteText(dst, "old");
            WriteText(backup, "new");
            fsx::BatchPlan plan;
            plan.AddSafeReplace(src.string(), dst.string(), !default_backup);
            fsx::RunOptions options;
            options.default_backup = default_backup;
            const auto result = fsx::Run(plan, options);
            EXPECT_FALSE(result.ok);
            EXPECT_NE(result.error.find("backup"), std::string::npos) << result.error;
            EXPECT_EQ(ReadText(dst), "old");
            EXPECT_EQ(ReadText(backup), "new");
            EXPECT_EQ(std::distance(std::filesystem::directory_iterator(root), std::filesystem::directory_iterator()),
                      2);
        }
    }
    std::filesystem::remove_all(root, ec);
}

TEST(FsxTests, SafeReplaceRejectsRetainedBackupHardLinkAlias)
{
    const auto root = TestRoot() / "backup_source_hardlink";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    const auto src = root / "source";
    const auto dst = root / "target";
    const auto backup = root / "target.bak";
    WriteText(src, "new");
    WriteText(dst, "old");
    std::filesystem::create_hard_link(src, backup, ec);
    if (ec)
        GTEST_SKIP() << "hard links unavailable: " << ec.message();
    fsx::BatchPlan plan;
    plan.AddSafeReplace(src.string(), dst.string(), true);
    const auto result = fsx::Run(plan);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(ReadText(src), "new");
    EXPECT_EQ(ReadText(dst), "old");
    EXPECT_EQ(ReadText(backup), "new");
    EXPECT_TRUE(std::filesystem::equivalent(src, backup));
    std::filesystem::remove_all(root, ec);
}

TEST(FsxTests, SafeReplaceRejectsRetainedBackupAncestorAndDestination)
{
    const auto root = TestRoot() / "backup_ancestor";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    const auto src = root / "target.bak" / "source";
    const auto dst = root / "target";
    WriteText(src, "new");
    WriteText(dst, "old");
    fsx::BatchPlan plan;
    plan.AddSafeReplace(src.string(), dst.string(), true);
    const auto ancestor = fsx::Run(plan);
    EXPECT_FALSE(ancestor.ok);
    EXPECT_NE(ancestor.error.find("backup"), std::string::npos) << ancestor.error;
    EXPECT_FALSE(ancestor.rollback_attempted && !ancestor.rollback_complete);
    EXPECT_EQ(ReadText(src), "new");
    EXPECT_EQ(ReadText(dst), "old");
    fsx::RunOptions options;
    options.backup_suffix.clear();
    const auto same_destination = fsx::Run(plan, options);
    EXPECT_FALSE(same_destination.ok);
    EXPECT_NE(same_destination.error.find("backup"), std::string::npos) << same_destination.error;
    EXPECT_EQ(ReadText(src), "new");
    EXPECT_EQ(ReadText(dst), "old");
    std::filesystem::remove_all(root, ec);
}

TEST(FsxTests, SafeReplaceRejectsRetainedBackupCanonicalSourceAlias)
{
    const auto root = TestRoot() / "backup_source_canonical";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    WriteText(root / "physical" / "target", "old");
    WriteText(root / "physical" / "target.bak", "new");
    std::filesystem::create_directory_symlink(root / "physical", root / "alias", ec);
    if (ec)
        GTEST_SKIP() << "directory symlinks unavailable: " << ec.message();
    fsx::BatchPlan plan;
    plan.AddSafeReplace((root / "alias" / "target.bak").string(), (root / "physical" / "target").string(), true);
    const auto result = fsx::Run(plan);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(ReadText(root / "physical" / "target"), "old");
    EXPECT_EQ(ReadText(root / "physical" / "target.bak"), "new");
    EXPECT_TRUE(std::filesystem::is_symlink(root / "alias"));
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
    EXPECT_NE(ReadText(journal).find("FSXJ4\n"), std::string::npos);

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

TEST(FsxTests, ReplacementJournalRecoversUnderGroupedAndClassicLocales)
{
    const auto root = TestRoot() / "recover_grouped_locale";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);

    for (const bool recover_under_classic : {false, true})
    {
        SCOPED_TRACE(recover_under_classic ? "recover under classic locale" : "recover under grouped locale");
        const ScopedGlobalLocale locale(std::locale(std::locale::classic(), new GroupedNumbers));
        const auto case_root = root / (recover_under_classic ? "classic" : "grouped");
        const auto target = case_root / "target.txt";
        const auto journal = case_root / "run.journal";
        WriteText(target, "original");
        const std::string replacement(12345, 'x');

        fsx::BatchPlan plan;
        plan.AddAtomicWrite(target.string(), replacement)
            .AddSafeReplace((case_root / "missing.txt").string(), (case_root / "later.txt").string());
        fsx::RunOptions options;
        options.fail_fast = false;
        options.journal_path = journal.string();
        const auto run = fsx::Run(plan, options);
        ASSERT_FALSE(run.ok);
        ASSERT_EQ(run.steps.size(), 2U);
        ASSERT_TRUE(run.steps[0].ok) << run.steps[0].error;
        EXPECT_FALSE(run.steps[1].ok);
        EXPECT_EQ(run.completed_steps, 1U);
        EXPECT_FALSE(run.rollback_attempted);
        EXPECT_EQ(ReadText(target), replacement);
        ASSERT_NE(ReadText(journal).find("UNDO|REPLACE|"), std::string::npos);
        for (const auto& entry : std::filesystem::directory_iterator(case_root))
            EXPECT_EQ(entry.path().filename().string().find(','), std::string::npos);

        if (recover_under_classic)
            std::locale::global(std::locale::classic());
        const auto recovered = fsx::RecoverFromJournal(journal.string());
        ASSERT_TRUE(recovered.ok) << recovered.error;
        EXPECT_TRUE(recovered.rollback_attempted);
        EXPECT_TRUE(recovered.rollback_complete);
        EXPECT_EQ(ReadText(target), "original");
        EXPECT_FALSE(std::filesystem::exists(journal));
        for (const auto& entry : std::filesystem::directory_iterator(case_root))
            EXPECT_EQ(entry.path(), target);
    }

    std::filesystem::remove_all(root, ec);
}

TEST(FsxTests, CommittedReplacementJournalCanBeReusedUnderGroupedAndClassicLocales)
{
    const auto root = TestRoot() / "committed_grouped_locale";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);

    for (const bool reuse_under_classic : {false, true})
    {
        SCOPED_TRACE(reuse_under_classic ? "reuse under classic locale" : "reuse under grouped locale");
        const ScopedGlobalLocale locale(std::locale(std::locale::classic(), new GroupedNumbers));
        const auto case_root = root / (reuse_under_classic ? "classic" : "grouped");
        const auto target = case_root / "target.txt";
        const auto journal = case_root / "run.journal";
        WriteText(target, "original");
        fsx::RunOptions options;
        options.journal_path = journal.string();
        options.keep_journal_on_success = true;
        fsx::BatchPlan first;
        first.AddAtomicWrite(target.string(), "first");
        const auto committed = fsx::Run(first, options);
        ASSERT_TRUE(committed.ok) << committed.error;
        ASSERT_NE(ReadText(journal).find("UNDO|REPLACE|"), std::string::npos);
        ASSERT_NE(ReadText(journal).find("\nCOMMIT\n"), std::string::npos);

        if (reuse_under_classic)
            std::locale::global(std::locale::classic());
        const auto recovered = fsx::RecoverFromJournal(journal.string());
        EXPECT_FALSE(recovered.ok);
        EXPECT_NE(recovered.error.find("completed transaction"), std::string::npos);
        EXPECT_EQ(ReadText(target), "first");
        fsx::BatchPlan second;
        second.AddAtomicWrite(target.string(), "second");
        const auto reused = fsx::Run(second, options);
        ASSERT_TRUE(reused.ok) << reused.error;
        EXPECT_EQ(ReadText(target), "second");
        EXPECT_NE(ReadText(journal).find("\nCOMMIT\n"), std::string::npos);
    }

    std::filesystem::remove_all(root, ec);
}

TEST(FsxTests, MalformedCommittedJournalCannotBeSilentlyReused)
{
    const auto root = TestRoot() / "malformed_committed_journal";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    const auto target = root / "target.txt";
    const auto journal = root / "run.journal";
    for (const std::string records : {"COMMIT\nUNDO|REMOVE|staging.tmp.1\n", "UNKNOWN|value\nCOMMIT\n"})
    {
        WriteText(target, "old");
        const std::string contents = "FSXJ4\n" + records;
        WriteText(journal, contents);
        fsx::BatchPlan plan;
        plan.AddAtomicWrite(target.string(), "new");
        fsx::RunOptions options;
        options.journal_path = journal.string();
        const auto result = fsx::Run(plan, options);
        EXPECT_FALSE(result.ok);
        EXPECT_EQ(ReadText(target), "old");
        EXPECT_EQ(ReadText(journal), contents);
    }
    std::filesystem::remove_all(root, ec);
}

TEST(FsxTests, JournalAliasesAreRejectedBeforeAnyMutation)
{
    const auto root = TestRoot() / "journal_aliases";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root);
    const auto target = root / "target.json";
    for (const bool keep : {false, true})
    {
        for (const bool existing : {false, true})
        {
            std::filesystem::remove(target, ec);
            const std::string original = "FSXJ4\nCOMMIT\n";
            if (existing)
                WriteText(target, original);
            fsx::BatchPlan plan;
            plan.AddAtomicWrite(target.string(), "new bytes");
            fsx::RunOptions options;
            options.journal_path = (root / "." / "target.json").string();
            options.keep_journal_on_success = keep;
            const auto run = fsx::Run(plan, options);
            EXPECT_FALSE(run.ok);
            EXPECT_NE(run.error.find("overlaps"), std::string::npos);
            EXPECT_TRUE(run.steps.empty());
            if (existing)
                EXPECT_EQ(ReadText(target), original);
            else
                EXPECT_FALSE(std::filesystem::exists(target));
        }
    }
}

TEST(FsxTests, JournalRelativeAndAbsoluteAliasesRejectNewOutputBeforeMutation)
{
    const auto root = TestRoot() / "journal_relative_absolute_aliases";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root);
    {
        const ScopedWorkingDirectory working_directory(root);
        const std::filesystem::path relative_target = "new-target.txt";
        const auto absolute_target = root / relative_target;
        for (const bool absolute_journal : {false, true})
        {
            for (const bool keep : {false, true})
            {
                fsx::BatchPlan plan;
                plan.AddAtomicWrite((absolute_journal ? relative_target : absolute_target).string(), "new bytes");
                fsx::RunOptions options;
                options.journal_path = (absolute_journal ? absolute_target : relative_target).string();
                options.keep_journal_on_success = keep;
                const auto run = fsx::Run(plan, options);
                EXPECT_FALSE(run.ok);
                EXPECT_NE(run.error.find("overlaps"), std::string::npos);
                EXPECT_TRUE(run.steps.empty());
                EXPECT_FALSE(std::filesystem::exists(absolute_target));
                EXPECT_TRUE(std::filesystem::is_empty(root));
            }
        }
    }
    std::filesystem::remove_all(root, ec);
}

TEST(FsxTests, JournalCannotOverlapTreeSourceDestinationOrBackup)
{
    const auto root = TestRoot() / "journal_tree_aliases";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    const auto source = root / "source";
    const auto destination = root / "destination";
    WriteText(source / "file", "original");
    WriteText(destination / "file", "old destination");
    fsx::BatchPlan plan;
    plan.AddCopyTree(source.string(), destination.string());
    for (const auto& journal : {source / "run.journal", destination / "run.journal", root})
    {
        fsx::RunOptions options;
        options.journal_path = journal.string();
        const auto run = fsx::Run(plan, options);
        EXPECT_FALSE(run.ok);
        EXPECT_NE(run.error.find("overlaps"), std::string::npos);
        EXPECT_TRUE(run.steps.empty());
        EXPECT_EQ(ReadText(source / "file"), "original");
        EXPECT_EQ(ReadText(destination / "file"), "old destination");
    }
    fsx::BatchPlan replace;
    replace.AddSafeReplace((source / "file").string(), (destination / "file").string(), true);
    fsx::RunOptions options;
    options.journal_path = (destination / "file.bak").string();
    EXPECT_FALSE(fsx::Run(replace, options).ok);
    EXPECT_FALSE(std::filesystem::exists(options.journal_path));
    EXPECT_EQ(ReadText(source / "file"), "original");
    EXPECT_EQ(ReadText(destination / "file"), "old destination");
}

TEST(FsxTests, JournalHardLinkAliasDoesNotTruncateSource)
{
    const auto root = TestRoot() / "journal_hardlink_alias";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    const auto source = root / "source";
    const auto journal = root / "run.journal";
    WriteText(source, "FSXJ4\nCOMMIT\n");
    std::filesystem::create_hard_link(source, journal, ec);
    if (ec)
        GTEST_SKIP() << "hard links unavailable: " << ec.message();
    fsx::BatchPlan plan;
    plan.AddCopyFile(source.string(), (root / "copied").string());
    fsx::RunOptions options;
    options.journal_path = journal.string();
    EXPECT_FALSE(fsx::Run(plan, options).ok);
    EXPECT_EQ(ReadText(source), "FSXJ4\nCOMMIT\n");
    EXPECT_FALSE(std::filesystem::exists(root / "copied"));
}

TEST(FsxTests, JournalRejectsAncestorWithTrailingSeparator)
{
    const auto root = TestRoot() / "journal_trailing_separator";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    WriteText(root / "tree" / "file", "original");
    fsx::BatchPlan plan;
    plan.AddRemovePath((root / "tree" / "").string());
    fsx::RunOptions options;
    options.journal_path = (root / "tree" / "run.journal").string();
    const auto run = fsx::Run(plan, options);
    EXPECT_FALSE(run.ok);
    EXPECT_NE(run.error.find("overlaps"), std::string::npos);
    EXPECT_TRUE(run.steps.empty());
    EXPECT_EQ(ReadText(root / "tree" / "file"), "original");
    EXPECT_FALSE(std::filesystem::exists(options.journal_path));
}

TEST(FsxTests, JournalSymlinkParentAliasIsRejectedBeforeCreatingOutput)
{
    const auto root = TestRoot() / "journal_symlink_parent_alias";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root / "physical");
    std::filesystem::create_directory_symlink(root / "physical", root / "alias", ec);
    if (ec)
        GTEST_SKIP() << "directory symbolic links unavailable: " << ec.message();
    fsx::BatchPlan plan;
    plan.AddAtomicWrite((root / "alias" / "output").string(), "new");
    fsx::RunOptions options;
    options.journal_path = (root / "physical" / "output").string();
    const auto run = fsx::Run(plan, options);
    EXPECT_FALSE(run.ok);
    EXPECT_NE(run.error.find("overlaps"), std::string::npos);
    EXPECT_TRUE(run.steps.empty());
    EXPECT_FALSE(std::filesystem::exists(root / "physical" / "output"));
    std::filesystem::remove_all(root, ec);
}

#if defined(_WIN32)
TEST(FsxTests, JournalCaseAliasUsesWindowsUnicodeComparison)
{
    const auto root = TestRoot() / "journal_unicode_alias";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root);
    fsx::BatchPlan plan;
    plan.AddAtomicWrite((root / L"\u00c4.json").string(), "new bytes");
    fsx::RunOptions options;
    options.journal_path = (root / L"\u00e4.JSON").string();
    const auto run = fsx::Run(plan, options);
    EXPECT_FALSE(run.ok);
    EXPECT_TRUE(run.steps.empty());
    EXPECT_TRUE(std::filesystem::is_empty(root));
}
#endif

#if defined(_WIN32)
TEST(FsxTests, JournalRejectsWindowsStreamJournalBeforeHeaderOrMutation)
{
    const auto root = TestRoot() / "journal_windows_stream_aliases";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::size_t case_number = 0;
    for (const auto* suffix : {":journal", "::$DATA", "::$data"})
    {
        for (const bool existing : {false, true})
        {
            for (int spelling = 0; spelling != 3; ++spelling)
            {
                SCOPED_TRACE(std::string(suffix) + ", existing=" + std::to_string(existing) +
                             ", spelling=" + std::to_string(spelling));
                const auto case_root = root / std::to_string(case_number++);
                const auto target = case_root / "target.txt";
                const auto journal = std::filesystem::path(target.string() + suffix);
                WriteText(case_root / "untouched.txt", "UNCHANGED");
                if (existing)
                {
                    WriteText(target, "OLD");
                    WriteText(std::filesystem::path(target.string() + ":preserve"), "PRESERVED STREAM");
                    WriteText(journal, "FSXJ4\nCOMMIT\n");
                    ASSERT_EQ(ReadText(journal), "FSXJ4\nCOMMIT\n");
                }
                fsx::BatchPlan plan;
                plan.AddAtomicWrite(target.string(), "NEW")
                    .AddSafeReplace((case_root / "missing.txt").string(), (case_root / "later.txt").string());
                fsx::RunOptions options;
                options.fail_fast = false;
                options.keep_journal_on_success = true;
                if (spelling == 0)
                    options.journal_path = journal.string();
                else if (spelling == 1)
                    options.journal_path = journal.lexically_relative(std::filesystem::current_path()).string();
                else
                    options.journal_path = ExtendedDrivePath(journal).string();
                ExpectJournalStreamRejectedWithoutMutation(plan, options, case_root);
            }
        }
    }
    std::filesystem::remove_all(root, ec);
}

TEST(FsxTests, JournalRejectsWindowsStreamsInEveryLaterBatchPathBeforeFirstStep)
{
    const auto root = TestRoot() / "journal_windows_later_stream_paths";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    const std::array<const char*, 10> roles{"atomic-dst", "replace-src", "replace-dst", "rename-src", "rename-dst",
                                            "copy-src",   "copy-dst",    "remove-dst",  "tree-src",   "tree-dst"};
    std::size_t case_number = 0;
    for (std::size_t role = 0; role != roles.size(); ++role)
    {
        for (const auto* suffix : {":journal", "::$DATA", "::$data"})
        {
            for (const bool existing : {false, true})
            {
                SCOPED_TRACE(std::string(roles[role]) + suffix + ", existing=" + std::to_string(existing));
                const auto case_root = root / std::to_string(case_number++);
                const auto source = case_root / "source.txt";
                const auto destination = case_root / "destination.txt";
                const auto victim = case_root / "victim.txt";
                const auto stream = std::filesystem::path(victim.string() + suffix);
                const auto journal = case_root / "run.journal";
                WriteText(source, "SOURCE OLD");
                WriteText(destination, "DESTINATION OLD");
                WriteText(std::filesystem::path(source.string() + ":preserve"), "SOURCE STREAM OLD");
                WriteText(std::filesystem::path(destination.string() + ":preserve"), "DESTINATION STREAM OLD");
                if (existing)
                {
                    WriteText(victim, "VICTIM OLD");
                    WriteText(std::filesystem::path(victim.string() + ":preserve"), "VICTIM STREAM OLD");
                    if (std::string(suffix) == ":journal")
                        WriteText(stream, "NAMED STREAM OLD");
                    WriteText(journal, "FSXJ4\nCOMMIT\n");
                }
                fsx::BatchPlan plan;
                // A validation confined to the current action would already
                // mutate this first step and create its previously absent parent.
                plan.AddAtomicWrite((case_root / "new-parent" / "first.txt").string(), "FIRST NEW");
                switch (role)
                {
                case 0:
                    plan.AddAtomicWrite(stream.string(), "STREAM NEW");
                    break;
                case 1:
                    plan.AddSafeReplace(stream.string(), destination.string());
                    break;
                case 2:
                    plan.AddSafeReplace(source.string(), stream.string());
                    break;
                case 3:
                    plan.AddRename(stream.string(), destination.string());
                    break;
                case 4:
                    plan.AddRename(source.string(), stream.string());
                    break;
                case 5:
                    plan.AddCopyFile(stream.string(), destination.string());
                    break;
                case 6:
                    plan.AddCopyFile(source.string(), stream.string());
                    break;
                case 7:
                    plan.AddRemovePath(stream.string());
                    break;
                case 8:
                    plan.AddCopyTree(stream.string(), destination.string());
                    break;
                case 9:
                    plan.AddCopyTree(source.string(), stream.string());
                    break;
                }
                fsx::RunOptions options;
                options.journal_path = journal.string();
                options.fail_fast = false;
                options.keep_journal_on_success = true;
                ExpectJournalStreamRejectedWithoutMutation(plan, options, case_root);
            }
        }
    }
    std::filesystem::remove_all(root, ec);
}

TEST(FsxTests, JournalRejectsWindowsSelectedBackupStreamsBeforeHeaderOrMutation)
{
    const auto root = TestRoot() / "journal_windows_backup_streams";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::size_t case_number = 0;
    for (const auto* suffix : {":backup", "::$DATA", "::$data"})
    {
        for (int selection = 0; selection != 3; ++selection)
        {
            SCOPED_TRACE(std::string(suffix) + ", selection=" + std::to_string(selection));
            const auto case_root = root / std::to_string(case_number++);
            const auto source = case_root / "source.txt";
            const auto destination = case_root / "destination.txt";
            const auto journal = case_root / "run.journal";
            WriteText(source, "SOURCE NEW");
            WriteText(destination, "DESTINATION OLD");
            WriteText(std::filesystem::path(destination.string() + ":preserve"), "DESTINATION STREAM OLD");
            if (std::string(suffix) == ":backup")
                WriteText(std::filesystem::path(destination.string() + suffix), "PREVIOUS BACKUP STREAM");
            WriteText(journal, "FSXJ4\nCOMMIT\n");
            fsx::BatchPlan plan;
            plan.AddAtomicWrite((case_root / "new-parent" / "first.txt").string(), "FIRST NEW")
                .AddSafeReplace(source.string(), destination.string(), selection != 1);
            fsx::RunOptions options;
            options.journal_path = journal.string();
            options.backup_suffix = suffix;
            options.default_backup = selection != 0;
            options.fail_fast = false;
            options.keep_journal_on_success = true;
            ExpectJournalStreamRejectedWithoutMutation(plan, options, case_root);
        }
    }
    std::filesystem::remove_all(root, ec);
}

TEST(FsxTests, UnusedWindowsStreamBackupSuffixDoesNotRejectJournaledActions)
{
    const auto root = TestRoot() / "journal_windows_unused_backup_suffix";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::size_t case_number = 0;
    for (const auto* suffix : {":backup", "::$DATA", "::$data"})
    {
        for (const bool atomic : {false, true})
        {
            SCOPED_TRACE(std::string(suffix) + ", atomic=" + std::to_string(atomic));
            const auto case_root = root / std::to_string(case_number++);
            const auto source = case_root / "source.txt";
            const auto target = case_root / "target.txt";
            const auto journal = case_root / "run.journal";
            WriteText(source, "NEW");
            WriteText(target, "OLD");
            fsx::BatchPlan plan;
            if (atomic)
                plan.AddAtomicWrite(target.string(), "NEW");
            else
                plan.AddSafeReplace(source.string(), target.string(), false);
            fsx::RunOptions options;
            options.journal_path = journal.string();
            options.backup_suffix = suffix;
            // AtomicWrite never selects the public SafeReplace backup suffix.
            options.default_backup = atomic;
            options.keep_journal_on_success = true;
            const auto result = fsx::Run(plan, options);
            ASSERT_TRUE(result.ok) << result.error;
            EXPECT_EQ(result.completed_steps, 1U);
            EXPECT_EQ(ReadText(target), "NEW");
            const auto bytes = ReadText(journal);
            ASSERT_GE(bytes.size(), 13U);
            EXPECT_EQ(bytes.substr(0, 6), "FSXJ4\n");
            EXPECT_EQ(bytes.substr(bytes.size() - 7), "COMMIT\n");
            if (std::string(suffix) == ":backup")
                EXPECT_FALSE(std::filesystem::exists(std::filesystem::path(target.string() + suffix)));
        }
    }
    std::filesystem::remove_all(root, ec);
}

TEST(FsxTests, NoJournalWindowsStreamSourceStillReachesNormalMissingSourceFailure)
{
    const auto root = TestRoot() / "no_journal_windows_stream_boundary";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::size_t case_number = 0;
    for (const auto* suffix : {":journal", "::$DATA", "::$data"})
    {
        SCOPED_TRACE(suffix);
        const auto case_root = root / std::to_string(case_number++);
        const auto destination = case_root / "destination.txt";
        WriteText(destination, "OLD");
        const auto before = JournalBoundaryTreeState(case_root);
        fsx::BatchPlan plan;
        plan.AddSafeReplace((case_root / (std::string("missing.txt") + suffix)).string(), destination.string());
        const auto result = fsx::Run(plan);
        EXPECT_FALSE(result.ok);
        ASSERT_EQ(result.steps.size(), 1U);
        EXPECT_EQ(result.completed_steps, 0U);
        EXPECT_NE(result.error.find("source file not found"), std::string::npos) << result.error;
        EXPECT_EQ(JournalBoundaryTreeState(case_root), before);
    }
    std::filesystem::remove_all(root, ec);
}

TEST(FsxTests, JournaledOrdinaryAndExtendedDrivePathsCommitAndRecoverOldBytes)
{
    const auto root = TestRoot() / "journal_windows_drive_controls";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    for (const bool extended : {false, true})
    {
        SCOPED_TRACE(extended ? "extended drive" : "ordinary drive");
        const auto case_root = root / (extended ? "extended" : "ordinary");
        const auto target = case_root / "target.txt";
        const auto source = case_root / "source.txt";
        const auto destination = case_root / "destination.txt";
        const auto journal = case_root / "run.journal";
        const auto path_arg = [extended](const std::filesystem::path& path)
        { return (extended ? ExtendedDrivePath(path) : path).string(); };
        WriteText(target, "OLD");
        WriteText(source, "SOURCE NEW");
        WriteText(destination, "DESTINATION OLD");
        fsx::BatchPlan successful;
        successful.AddAtomicWrite(path_arg(target), "COMMITTED")
            .AddSafeReplace(path_arg(source), path_arg(destination), true);
        fsx::RunOptions options;
        options.journal_path = path_arg(journal);
        options.backup_suffix = ".keep";
        options.keep_journal_on_success = true;
        const auto committed = fsx::Run(successful, options);
        ASSERT_TRUE(committed.ok) << committed.error;
        EXPECT_EQ(committed.completed_steps, 2U);
        EXPECT_EQ(ReadText(target), "COMMITTED");
        EXPECT_EQ(ReadText(destination), "SOURCE NEW");
        EXPECT_EQ(ReadText(std::filesystem::path(destination.string() + ".keep")), "DESTINATION OLD");
        EXPECT_NE(ReadText(journal).find("\nCOMMIT\n"), std::string::npos);

        WriteText(target, "OLD");
        fsx::BatchPlan interrupted;
        interrupted.AddAtomicWrite(path_arg(target), "NEW")
            .AddSafeReplace(path_arg(case_root / "missing.txt"), path_arg(case_root / "later.txt"));
        options.fail_fast = false;
        const auto failed = fsx::Run(interrupted, options);
        ASSERT_FALSE(failed.ok);
        EXPECT_EQ(failed.completed_steps, 1U);
        EXPECT_EQ(ReadText(target), "NEW");
        EXPECT_FALSE(failed.rollback_attempted);
        const auto recovered = fsx::RecoverFromJournal(path_arg(journal));
        ASSERT_TRUE(recovered.ok) << recovered.error;
        EXPECT_TRUE(recovered.rollback_complete);
        EXPECT_EQ(ReadText(target), "OLD");
        EXPECT_EQ(ReadText(std::filesystem::path(destination.string() + ".keep")), "DESTINATION OLD");
        EXPECT_FALSE(std::filesystem::exists(journal));
    }
    std::filesystem::remove_all(root, ec);
}
#else
TEST(FsxTests, JournaledPosixColonPathsAndRetainedBackupsCommitAndRecoverOldBytes)
{
    const auto root = TestRoot() / "journal_posix_colon_controls";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::size_t case_number = 0;
    for (const auto* suffix : {":journal", "::$DATA", "::$data"})
    {
        SCOPED_TRACE(suffix);
        const auto case_root = root / std::to_string(case_number++);
        const auto target = case_root / (std::string("target.txt") + suffix);
        const auto source = case_root / (std::string("source.txt") + suffix);
        const auto destination = case_root / (std::string("destination.txt") + suffix);
        const auto journal = case_root / (std::string("run.journal") + suffix);
        WriteText(target, "OLD");
        WriteText(source, "SOURCE NEW");
        WriteText(destination, "DESTINATION OLD");
        fsx::BatchPlan successful;
        successful.AddAtomicWrite(target.string(), "COMMITTED")
            .AddSafeReplace(source.string(), destination.string(), true);
        fsx::RunOptions options;
        options.journal_path = journal.string();
        options.backup_suffix = ":backup";
        options.keep_journal_on_success = true;
        const auto committed = fsx::Run(successful, options);
        ASSERT_TRUE(committed.ok) << committed.error;
        EXPECT_EQ(committed.completed_steps, 2U);
        EXPECT_EQ(ReadText(target), "COMMITTED");
        EXPECT_EQ(ReadText(destination), "SOURCE NEW");
        const auto backup = std::filesystem::path(destination.string() + options.backup_suffix);
        EXPECT_EQ(ReadText(backup), "DESTINATION OLD");
        EXPECT_NE(ReadText(journal).find("\nCOMMIT\n"), std::string::npos);

        WriteText(target, "OLD");
        fsx::BatchPlan interrupted;
        interrupted.AddAtomicWrite(target.string(), "NEW")
            .AddSafeReplace((case_root / "missing.txt").string(), (case_root / "later.txt").string());
        options.fail_fast = false;
        const auto failed = fsx::Run(interrupted, options);
        ASSERT_FALSE(failed.ok);
        EXPECT_EQ(failed.completed_steps, 1U);
        EXPECT_EQ(ReadText(target), "NEW");
        const auto recovered = fsx::RecoverFromJournal(journal.string());
        ASSERT_TRUE(recovered.ok) << recovered.error;
        EXPECT_TRUE(recovered.rollback_complete);
        EXPECT_EQ(ReadText(target), "OLD");
        EXPECT_EQ(ReadText(backup), "DESTINATION OLD");
        EXPECT_FALSE(std::filesystem::exists(journal));
    }
    std::filesystem::remove_all(root, ec);
}
#endif

TEST(FsxTests, RecoverFromJournalMaintainsV1AndV2Compatibility)
{
    const auto root = TestRoot() / "recover_legacy_journals";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);

    const auto v1_target = root / "v1-target.txt";
    const auto v1_journal = root / "v1.journal";
    WriteText(v1_target, "transient");
    WriteText(v1_journal, "FSXJ1\nUNDO|REMOVE|" + v1_target.string() + "\n");

    const auto v1_recovered = fsx::RecoverFromJournal(v1_journal.string());
    ASSERT_TRUE(v1_recovered.ok) << v1_recovered.error;
    EXPECT_FALSE(std::filesystem::exists(v1_target));
    EXPECT_FALSE(std::filesystem::exists(v1_journal));

    const auto v2_backup = root / "v2-backup.txt";
    const auto v2_target = root / "v2-target.txt";
    const auto v2_journal = root / "v2.journal";
    WriteText(v2_backup, "original");
    WriteText(v2_journal, "FSXJ2\nUNDO|MOVE|" + v2_backup.string() + "|" + v2_target.string() + "\n");

    const auto v2_recovered = fsx::RecoverFromJournal(v2_journal.string());
    ASSERT_TRUE(v2_recovered.ok) << v2_recovered.error;
    EXPECT_FALSE(std::filesystem::exists(v2_backup));
    EXPECT_EQ(ReadText(v2_target), "original");
    EXPECT_FALSE(std::filesystem::exists(v2_journal));

    std::filesystem::remove_all(root, ec);
}

TEST(FsxTests, GeneratedJournalRecoversRelativeOperationsAfterWorkingDirectoryChanges)
{
    const auto root = TestRoot() / "relative_recovery";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root / "other");
    for (const bool existing : {false, true})
    {
        const auto job = root / (existing ? "existing" : "new");
        std::filesystem::create_directories(job / "data");
        const auto target = job / "data" / "target.txt";
        const auto journal = job / "data" / "run.journal";
        if (existing)
            WriteText(target, "old");
        fsx::RunResult run;
        {
            ScopedWorkingDirectory cwd(job);
            fsx::BatchPlan plan;
            plan.AddAtomicWrite("data/target.txt", "new").AddSafeReplace("missing.txt", "later.txt");
            fsx::RunOptions options;
            options.fail_fast = false;
            options.journal_path = "data/run.journal";
            run = fsx::Run(plan, options);
        }
        ASSERT_FALSE(run.ok);
        ASSERT_EQ(ReadText(target), "new");
        fsx::RunResult recovered;
        {
            ScopedWorkingDirectory cwd(root / "other");
            recovered = fsx::RecoverFromJournal(journal.string());
        }
        ASSERT_TRUE(recovered.ok) << recovered.error;
        EXPECT_TRUE(recovered.rollback_complete);
        EXPECT_FALSE(std::filesystem::exists(journal));
        if (existing)
            EXPECT_EQ(ReadText(target), "old");
        else
            EXPECT_FALSE(std::filesystem::exists(target));
    }
    std::filesystem::remove_all(root, ec);
}

TEST(FsxTests, RelativeV4RecordsAreRejectedBeforeAnyMutation)
{
    const auto root = TestRoot() / "relative_v4_records";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    const auto staging = root / "staging.tmp.1";
    const auto journal = root / "run.journal";
    WriteText(staging, "keep");
    const auto contents = "FSXJ4\nUNDO|MOVE|relative.tmp.1|relative-target\nUNDO|REMOVE|" + staging.string() + "\n";
    WriteText(journal, contents);
    const auto recovered = fsx::RecoverFromJournal(journal.string());
    EXPECT_FALSE(recovered.ok);
    EXPECT_FALSE(recovered.rollback_attempted);
    EXPECT_NE(recovered.error.find("absolute undo paths"), std::string::npos);
    EXPECT_EQ(ReadText(staging), "keep");
    EXPECT_EQ(ReadText(journal), contents);
    std::filesystem::remove_all(root, ec);
}

TEST(FsxTests, LegacyRelativeRecordsRemainUsableFromOriginalWorkingDirectory)
{
    const auto root = TestRoot() / "legacy_relative_records";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root);
    for (const std::string header : {"FSXJ1", "FSXJ2", "FSXJ3"})
    {
        const auto source = root / "source.tmp.1";
        const auto target = root / "target.txt";
        const auto journal = root / "run.journal";
        std::filesystem::remove(target, ec);
        WriteText(source, "old");
        WriteText(journal, header + "\nUNDO|MOVE|source.tmp.1|target.txt\n");
        fsx::RunResult recovered;
        {
            ScopedWorkingDirectory cwd(root);
            recovered = fsx::RecoverFromJournal(journal.string());
        }
        EXPECT_TRUE(recovered.ok) << header << ": " << recovered.error;
        EXPECT_EQ(ReadText(target), "old");
        EXPECT_FALSE(std::filesystem::exists(source));
    }
    std::filesystem::remove_all(root, ec);
}

TEST(FsxTests, RecoverFromJournalV3PreservesNewDestinationConflict)
{
    const auto root = TestRoot() / "recover_v3_conflict";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);

    const auto backup = root / "old.tmp.1";
    const auto target = root / "target.txt";
    const auto journal = root / "run.journal";
    WriteText(backup, "original");
    WriteText(target, "appeared-during-recovery");
    WriteText(journal, "FSXJ3\nUNDO|MOVE|" + backup.string() + "|" + target.string() + "\n");

    const auto recovered = fsx::RecoverFromJournal(journal.string());
    EXPECT_FALSE(recovered.ok);
    EXPECT_NE(recovered.error.find("conflict"), std::string::npos);
    EXPECT_EQ(ReadText(backup), "original");
    EXPECT_EQ(ReadText(target), "appeared-during-recovery");
    EXPECT_TRUE(std::filesystem::exists(journal));

    std::filesystem::remove_all(root, ec);
}

TEST(FsxTests, RecoverFromJournalV3ManualMoveJournalIsCompatible)
{
    const auto root = TestRoot() / "recover_v3_manual";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);

    const auto staging = root / "staging.tmp.1";
    const auto target = root / "target.txt";
    const auto journal = root / "run.journal";
    WriteText(staging, "original");
    WriteText(journal, "FSXJ3\nUNDO|MOVE|" + staging.string() + "|" + target.string() + "\n");

    const auto recovered = fsx::RecoverFromJournal(journal.string());
    ASSERT_TRUE(recovered.ok) << recovered.error;
    EXPECT_FALSE(std::filesystem::exists(staging));
    EXPECT_EQ(ReadText(target), "original");
    EXPECT_FALSE(std::filesystem::exists(journal));

    std::filesystem::remove_all(root, ec);
}

TEST(FsxTests, LegacyBestEffortRecoveryReportsFailureAndKeepsEvidence)
{
    const auto root = TestRoot() / "recover_legacy_failure";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    const auto nonempty = root / "nonempty";
    const auto independent = root / "independent";
    const auto journal = root / "run.journal";
    WriteText(nonempty / "keep.txt", "keep");
    WriteText(independent, "temporary");
    WriteText(journal, "FSXJ2\nUNDO|REMOVE|" + independent.string() + "\nUNDO|REMOVE|" + nonempty.string() + "\n");

    const auto recovered = fsx::RecoverFromJournal(journal.string());
    EXPECT_FALSE(recovered.ok);
    EXPECT_TRUE(recovered.rollback_attempted);
    EXPECT_FALSE(recovered.rollback_complete);
    EXPECT_FALSE(recovered.rollback_error.empty());
    EXPECT_TRUE(std::filesystem::exists(journal));
    EXPECT_EQ(ReadText(nonempty / "keep.txt"), "keep");
    EXPECT_FALSE(std::filesystem::exists(independent));
    ASSERT_EQ(recovered.steps.size(), 2U);
    EXPECT_FALSE(recovered.steps[0].rolled_back);
    EXPECT_FALSE(recovered.steps[0].rollback_error.empty());
    EXPECT_TRUE(recovered.steps[1].rolled_back);
    std::filesystem::remove_all(root, ec);
}

TEST(FsxTests, RecoveryRejectsUnknownRecordsBeforeAnyMutation)
{
    const auto root = TestRoot() / "recover_unknown_record";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    const auto staging = root / "staging.tmp.1";
    const auto journal = root / "run.journal";
    for (const std::string header : {"FSXJ1", "FSXJ2", "FSXJ3", "FSXJ4"})
    {
        WriteText(staging, "keep");
        WriteText(journal, header + "\nUNDO|REMOVE|" + staging.string() + "\nUNKNOWN|value\n");
        const auto recovered = fsx::RecoverFromJournal(journal.string());
        EXPECT_FALSE(recovered.ok) << header;
        EXPECT_FALSE(recovered.rollback_attempted) << header;
        EXPECT_EQ(ReadText(staging), "keep") << header;
        EXPECT_TRUE(std::filesystem::exists(journal)) << header;
    }
    std::filesystem::remove_all(root, ec);
}

TEST(FsxTests, RecoveryRejectsDuplicateAndOutOfOrderRestoredMarkers)
{
    const auto root = TestRoot() / "recover_invalid_marker";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    const auto old = root / "old.tmp.1";
    const auto newer = root / "new.tmp.1";
    const auto staging = root / "restore.tmp.1";
    const auto target = root / "target.txt";
    const auto journal = root / "run.journal";
    const std::string cleanup =
        "UNDO|REMOVE|" + old.string() + "\nUNDO|REMOVE|" + newer.string() + "\nUNDO|REMOVE|" + staging.string() + "\n";
    const std::string replace = "UNDO|REPLACE|" + old.string() + "|" + target.string() + "|" + newer.string() + "|" +
                                staging.string() + "|1:2:3:4:5:6:7|1:2:3:4:5:6:7\n";
    const std::string marker = "RESTORED|" + old.string() + "\n";
    for (const auto& records :
         {cleanup + marker + replace, cleanup + replace + marker + marker, cleanup + replace + replace,
          cleanup + replace + "RESTORED|unknown.tmp.1\n", cleanup + replace + "COMMIT\n" + marker})
    {
        WriteText(old, "old");
        WriteText(newer, "new");
        WriteText(target, "new");
        WriteText(journal, "FSXJ4\n" + records);
        const auto recovered = fsx::RecoverFromJournal(journal.string());
        EXPECT_FALSE(recovered.ok);
        EXPECT_FALSE(recovered.rollback_attempted);
        EXPECT_EQ(ReadText(target), "new");
        EXPECT_EQ(ReadText(old), "old");
        EXPECT_EQ(ReadText(newer), "new");
        EXPECT_TRUE(std::filesystem::exists(journal));
    }
    std::filesystem::remove_all(root, ec);
}

TEST(FsxTests, RollbackNeverMarksSkippedForwardStepsAndCountsBusinessSteps)
{
    const auto root = TestRoot() / "rollback_skipped";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    WriteText(root / "existing.txt", "old");
    fsx::BatchPlan plan;
    plan.AddAtomicWrite((root / "existing.txt").string(), "skipped")
        .AddAtomicWrite((root / "created.txt").string(), "new")
        .AddSafeReplace((root / "missing.txt").string(), (root / "later.txt").string());
    fsx::RunOptions options;
    options.conflict_policy = fsx::ConflictPolicy::Skip;
    const auto result = fsx::Run(plan, options);
    ASSERT_FALSE(result.ok);
    ASSERT_EQ(result.steps.size(), 3U);
    EXPECT_TRUE(result.steps[0].skipped);
    EXPECT_FALSE(result.steps[0].rollback_attempted);
    EXPECT_FALSE(result.steps[0].rolled_back);
    EXPECT_TRUE(result.steps[1].rolled_back);
    EXPECT_EQ(result.rolled_back_steps, 1U);
    EXPECT_TRUE(result.rollback_complete);
    EXPECT_EQ(ReadText(root / "existing.txt"), "old");
    EXPECT_FALSE(std::filesystem::exists(root / "created.txt"));
    std::filesystem::remove_all(root, ec);
}

TEST(FsxTests, FailedTreeStepReportsItsCompletedPartialRollback)
{
    const auto root = TestRoot() / "rollback_partial_step";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    WriteText(root / "source" / "a.txt", "new");
    WriteText(root / "source" / "z" / "item.txt", "later");
    WriteText(root / "destination" / "a.txt", "old");
    WriteText(root / "destination" / "z", "blocked-parent");
    fsx::BatchPlan plan;
    plan.AddCopyTree((root / "source").string(), (root / "destination").string());
    const auto result = fsx::Run(plan);
    ASSERT_FALSE(result.ok);
    ASSERT_EQ(result.steps.size(), 1U);
    EXPECT_FALSE(result.steps[0].ok);
    EXPECT_TRUE(result.steps[0].rollback_attempted);
    EXPECT_TRUE(result.steps[0].rolled_back);
    EXPECT_TRUE(result.rollback_complete);
    EXPECT_EQ(result.rolled_back_steps, 1U);
    EXPECT_EQ(ReadText(root / "destination" / "a.txt"), "old");
    EXPECT_EQ(ReadText(root / "destination" / "z"), "blocked-parent");
    std::filesystem::remove_all(root, ec);
}

TEST(FsxTests, AtomicWriteRollbackRestoresFileReplacedByCreatedParents)
{
    const auto root = TestRoot() / "rollback_atomic_parents";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    const auto target = root / "original";
    WriteText(target, "old-file");
    fsx::BatchPlan plan;
    plan.AddRemovePath(target.string())
        .AddAtomicWrite((target / "nested" / "new.txt").string(), "new")
        .AddSafeReplace((root / "missing.txt").string(), (root / "later.txt").string());
    const auto result = fsx::Run(plan);
    ASSERT_FALSE(result.ok);
    EXPECT_TRUE(result.rollback_complete) << result.rollback_error;
    EXPECT_EQ(ReadText(target), "old-file");
    EXPECT_EQ(result.rolled_back_steps, 2U);
    std::filesystem::remove_all(root, ec);
}

TEST(FsxTests, AtomicWriteRejectsExistingDirectoryWithoutRemovingIt)
{
    const auto root = TestRoot() / "atomic_directory";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    WriteText(root / "target" / "keep.txt", "keep");
    fsx::BatchPlan plan;
    plan.AddAtomicWrite((root / "target").string(), "new");
    const auto result = fsx::Run(plan);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(ReadText(root / "target" / "keep.txt"), "keep");
    EXPECT_TRUE(result.rollback_complete);
    std::filesystem::remove_all(root, ec);
}

TEST(FsxTests, AtomicWriteRejectsDanglingSymlinkWithoutReplacingIt)
{
    const auto root = TestRoot() / "atomic_dangling_link";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
    const auto target = root / "target.txt";
    const auto missing = root / "missing.txt";
    std::filesystem::create_symlink(missing, target, ec);
    if (ec)
        GTEST_SKIP() << "symbolic links unavailable: " << ec.message();
    fsx::BatchPlan plan;
    plan.AddAtomicWrite(target.string(), "new");
    const auto result = fsx::Run(plan);
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(std::filesystem::is_symlink(std::filesystem::symlink_status(target)));
    EXPECT_EQ(std::filesystem::read_symlink(target), missing);
    EXPECT_FALSE(std::filesystem::exists(missing));
    std::filesystem::remove_all(root, ec);
}

TEST(FsxTests, RepeatedAtomicWritesRestoreOriginalTargetOnFailure)
{
    const auto root = TestRoot() / "atomic_repeated_rollback";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    const auto target = root / "target.txt";
    WriteText(target, "original");
    fsx::BatchPlan plan;
    plan.AddAtomicWrite(target.string(), "first")
        .AddAtomicWrite(target.string(), "second")
        .AddSafeReplace((root / "missing.txt").string(), (root / "later.txt").string());
    fsx::RunOptions options;
    options.journal_path = (root / "run.journal").string();
    const auto result = fsx::Run(plan, options);
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.rollback_complete) << result.rollback_error;
    EXPECT_EQ(result.rolled_back_steps, 2U);
    EXPECT_EQ(ReadText(target), "original");
    EXPECT_FALSE(std::filesystem::exists(options.journal_path));
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
    EXPECT_TRUE(result.rollback_complete);
    ASSERT_EQ(result.steps.size(), 2U);
    EXPECT_TRUE(result.steps[0].rolled_back);
    EXPECT_EQ(result.rolled_back_steps, 1U);

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

TEST(FsxTests, CreateLinkOverwriteRejectsSourceAliases)
{
    const auto root = TestRoot() / "link_source_alias";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    for (const auto type : {fsx::LinkType::Hard, fsx::LinkType::Symbolic})
    {
        for (const bool lexical_alias : {false, true})
        {
            const auto source = root / "source";
            const auto link = lexical_alias ? root / "." / "source" : source;
            WriteText(source, "SOURCE-SENTINEL");
            const auto result = fsx::CreateLink(source.string(), link.string(), type, true);
            EXPECT_FALSE(result.ok);
            EXPECT_EQ(ReadText(source), "SOURCE-SENTINEL");
            EXPECT_FALSE(std::filesystem::is_symlink(source));
        }
    }
    const auto alias = root / "hard-alias";
    std::filesystem::create_hard_link(root / "source", alias, ec);
    if (ec)
        GTEST_SKIP() << "hard links unavailable: " << ec.message();
    for (const auto type : {fsx::LinkType::Hard, fsx::LinkType::Symbolic})
    {
        EXPECT_FALSE(fsx::CreateLink((root / "source").string(), alias.string(), type, true).ok);
        EXPECT_EQ(ReadText(root / "source"), "SOURCE-SENTINEL");
        EXPECT_EQ(ReadText(alias), "SOURCE-SENTINEL");
        EXPECT_TRUE(std::filesystem::equivalent(root / "source", alias));
    }
    std::filesystem::remove_all(root, ec);
}

TEST(FsxTests, CreateLinkOverwriteRejectsCanonicalSourceAlias)
{
    const auto root = TestRoot() / "link_canonical_source_alias";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    const auto source = root / "physical" / "source";
    WriteText(source, "SOURCE-SENTINEL");
    std::filesystem::create_directory_symlink(root / "physical", root / "alias", ec);
    if (ec)
        GTEST_SKIP() << "directory symlinks unavailable: " << ec.message();
    for (const auto type : {fsx::LinkType::Hard, fsx::LinkType::Symbolic})
    {
        const auto result = fsx::CreateLink(source.string(), (root / "alias" / "source").string(), type, true);
        EXPECT_FALSE(result.ok);
        EXPECT_EQ(ReadText(source), "SOURCE-SENTINEL");
        EXPECT_TRUE(std::filesystem::is_symlink(root / "alias"));
    }
    std::filesystem::remove_all(root, ec);
}

TEST(FsxTests, CreateLinkOverwriteRejectsRelativeSymbolicSelfReference)
{
    const auto root = TestRoot() / "link_relative_self_reference";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    WriteText(root / "destination", "OLD");
    const auto result = fsx::CreateLink("destination", (root / "destination").string(), fsx::LinkType::Symbolic, true);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(ReadText(root / "destination"), "OLD");
    EXPECT_FALSE(std::filesystem::is_symlink(root / "destination"));
    std::filesystem::remove_all(root, ec);
}

TEST(FsxTests, CreateLinkFailedCandidatePreservesExistingDestination)
{
    const auto root = TestRoot() / "link_failed_candidate";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    const auto link = root / "destination";
    WriteText(link, "OLD");
    const auto missing = fsx::CreateLink((root / "missing").string(), link.string(), fsx::LinkType::Hard, true);
    EXPECT_FALSE(missing.ok);
    EXPECT_EQ(ReadText(link), "OLD");
    const auto invalid = fsx::CreateLink("", link.string(), fsx::LinkType::Symbolic, true);
    EXPECT_FALSE(invalid.ok);
    EXPECT_EQ(ReadText(link), "OLD");
    EXPECT_EQ(std::distance(std::filesystem::directory_iterator(root), std::filesystem::directory_iterator()), 1);
    std::filesystem::remove_all(root, ec);
}

#if defined(_WIN32)
TEST(FsxTests, CreateLinkFailedPublicationPreservesExistingDestination)
{
    const auto root = TestRoot() / "link_failed_publication";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    const auto source = root / "source";
    const auto destination = root / "destination";
    WriteText(source, "SOURCE");
    WriteText(destination, "OLD");
    HANDLE reader = CreateFileW(destination.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    ASSERT_NE(reader, INVALID_HANDLE_VALUE);
    const auto result = fsx::CreateLink(source.string(), destination.string(), fsx::LinkType::Hard, true);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(ReadText(source), "SOURCE");
    EXPECT_EQ(ReadText(destination), "OLD");
    EXPECT_EQ(std::distance(std::filesystem::directory_iterator(root), std::filesystem::directory_iterator()), 2);
    CloseHandle(reader);
    std::filesystem::remove_all(root, ec);
}
#endif

TEST(FsxTests, CreateLinkOverwritePreservesNonemptyDirectoryAndReplacesEmptyDirectory)
{
    const auto root = TestRoot() / "link_directory_destination";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    WriteText(root / "source", "SOURCE");
    WriteText(root / "destination" / "keep", "KEEP");
    const auto nonempty =
        fsx::CreateLink((root / "source").string(), (root / "destination").string(), fsx::LinkType::Hard, true);
    EXPECT_FALSE(nonempty.ok);
    EXPECT_EQ(ReadText(root / "source"), "SOURCE");
    EXPECT_EQ(ReadText(root / "destination" / "keep"), "KEEP");
    std::filesystem::remove(root / "destination" / "keep", ec);
    const auto empty =
        fsx::CreateLink((root / "source").string(), (root / "destination").string(), fsx::LinkType::Hard, true);
    ASSERT_TRUE(empty.ok) << empty.error;
    EXPECT_EQ(ReadText(root / "source"), "SOURCE");
    EXPECT_EQ(ReadText(root / "destination"), "SOURCE");
    EXPECT_EQ(std::distance(std::filesystem::directory_iterator(root), std::filesystem::directory_iterator()), 2);
    std::filesystem::remove_all(root, ec);
}

#if defined(__linux__)
TEST(FsxTests, CreateLinkCrossDeviceFailurePreservesExistingDestination)
{
    const auto root = TestRoot() / "link_cross_device_source";
    const auto other = std::filesystem::path("/dev/shm/toolx-fsx-link-cross-device-test");
    std::error_code ec;
    if (!std::filesystem::is_directory(other.parent_path(), ec))
        GTEST_SKIP() << "/dev/shm unavailable";
    std::filesystem::remove_all(root, ec);
    std::filesystem::remove_all(other, ec);
    WriteText(root / "source", "SOURCE");
    WriteText(other / "destination", "OLD");
    std::filesystem::create_hard_link(root / "source", other / "probe", ec);
    if (ec != std::errc::cross_device_link)
    {
        std::filesystem::remove_all(root, ec);
        std::filesystem::remove_all(other, ec);
        GTEST_SKIP() << "separate writable filesystem unavailable";
    }
    const auto result =
        fsx::CreateLink((root / "source").string(), (other / "destination").string(), fsx::LinkType::Hard, true);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(ReadText(root / "source"), "SOURCE");
    EXPECT_EQ(ReadText(other / "destination"), "OLD");
    EXPECT_EQ(std::distance(std::filesystem::directory_iterator(other), std::filesystem::directory_iterator()), 1);
    std::filesystem::remove_all(root, ec);
    std::filesystem::remove_all(other, ec);
}
#endif

TEST(FsxTests, CreateLinkOverwriteReplacesDistinctDestination)
{
    const auto root = TestRoot() / "link_overwrite_distinct";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    const auto source = root / "source";
    const auto link = root / "destination";
    WriteText(source, "new");
    WriteText(link, "old");
    const auto hard = fsx::CreateLink(source.string(), link.string(), fsx::LinkType::Hard, true);
    ASSERT_TRUE(hard.ok) << hard.error;
    EXPECT_EQ(ReadText(source), "new");
    EXPECT_EQ(ReadText(link), "new");
    EXPECT_TRUE(std::filesystem::equivalent(source, link));
    std::filesystem::remove(link, ec);
    WriteText(link, "old");
    const auto symbolic = fsx::CreateLink("source", link.string(), fsx::LinkType::Symbolic, true);
    if (!symbolic.ok && !fsx::QueryCapabilities().symbolic_link)
        GTEST_SKIP() << "symbolic links unavailable: " << symbolic.error;
    ASSERT_TRUE(symbolic.ok) << symbolic.error;
    EXPECT_EQ(ReadText(source), "new");
    EXPECT_EQ(ReadText(link), "new");
    EXPECT_TRUE(std::filesystem::is_symlink(link));
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

TEST(FsxTests, TarArchivePreservesEmptyFiles)
{
    const auto root = TestRoot() / "tar_empty_files";
    const auto src = root / "src";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    WriteText(src / "empty.txt", "");
    WriteText(src / "nested" / "empty.txt", "");
    std::string payload(8193, 'P');
    payload[4096] = '\0';
    WriteText(src / "last.txt", payload);

    const auto created = fsx::CreateArchive(src.string(), (root / "bundle.tar").string());
    ASSERT_TRUE(created.ok) << created.error;
    const auto extracted = fsx::ExtractArchive((root / "bundle.tar").string(), (root / "out").string());
    ASSERT_TRUE(extracted.ok) << extracted.error;
    EXPECT_TRUE(std::filesystem::is_regular_file(root / "out" / "empty.txt"));
    EXPECT_EQ(std::filesystem::file_size(root / "out" / "empty.txt"), 0U);
    EXPECT_TRUE(std::filesystem::is_regular_file(root / "out" / "nested" / "empty.txt"));
    EXPECT_EQ(std::filesystem::file_size(root / "out" / "nested" / "empty.txt"), 0U);
    EXPECT_EQ(ReadText(root / "out" / "last.txt"), payload);

    const auto single = fsx::CreateArchive((src / "empty.txt").string(), (root / "single.tar").string());
    ASSERT_TRUE(single.ok) << single.error;
    ASSERT_TRUE(fsx::ExtractArchive((root / "single.tar").string(), (root / "single-out").string()).ok);
    EXPECT_TRUE(std::filesystem::is_regular_file(root / "single-out" / "empty.txt"));
    EXPECT_EQ(std::filesystem::file_size(root / "single-out" / "empty.txt"), 0U);
    std::filesystem::remove_all(root, ec);
}

TEST(FsxTests, ExtractionPreservesItsInputArchiveAndHardLinkAliases)
{
    const auto root = TestRoot() / "tar_input_alias";
    std::error_code cleanup_ec;
    std::filesystem::remove_all(root, cleanup_ec);
    for (const bool hard_link : {false, true})
    {
        for (const auto policy : {fsx::ConflictPolicy::Overwrite, fsx::ConflictPolicy::Skip, fsx::ConflictPolicy::Fail})
        {
            const auto trial = root / (std::to_string(hard_link) + "-" + std::to_string(static_cast<int>(policy)));
            const auto source = trial / "src" / (hard_link ? "payload.txt" : "bundle.tar");
            const auto output = trial / "out";
            const auto archive = output / "bundle.tar";
            WriteText(source, "archive must survive");
            std::filesystem::create_directories(output);
            ASSERT_TRUE(fsx::CreateArchive(source.string(), archive.string()).ok);
            if (hard_link)
            {
                std::error_code ec;
                std::filesystem::create_hard_link(archive, output / "payload.txt", ec);
                ASSERT_FALSE(ec) << ec.message();
            }
            const auto original = ReadText(archive);
            fsx::ArchiveOptions options;
            options.conflict_policy = policy;
            const auto result = fsx::ExtractArchive(archive.string(), output.string(), options);
            EXPECT_EQ(result.ok, policy == fsx::ConflictPolicy::Skip);
            EXPECT_TRUE(ReadText(archive) == original) << "extraction changed the archive bytes";
        }
    }

    const auto source = root / "ancestor-src" / "old";
    const auto output = root / "ancestor-out";
    const auto archive = output / "old" / "bundle.tar";
    WriteText(source, "replacement file");
    std::filesystem::create_directories(archive.parent_path());
    ASSERT_TRUE(fsx::CreateArchive(source.string(), archive.string()).ok);
    const auto original = ReadText(archive);
    EXPECT_FALSE(fsx::ExtractArchive(archive.string(), output.string()).ok);
    EXPECT_TRUE(ReadText(archive) == original);
}

TEST(FsxTests, ArchiveInputReadErrorsAndBufferedOutputFailuresAreReported)
{
    const auto root = TestRoot() / "tar_io_errors";
    const auto directory = root / "directory.tar";
    std::filesystem::create_directories(directory);
    EXPECT_FALSE(fsx::ExtractArchive(directory.string(), (root / "out").string()).ok);
#if !defined(_WIN32)
    if (std::filesystem::exists("/dev/full"))
    {
        WriteText(root / "source.txt", "buffered content");
        EXPECT_FALSE(fsx::CreateArchive((root / "source.txt").string(), "/dev/full").ok);
    }
#endif
}

TEST(FsxTests, TarSingleFileRootOptionDoesNotCreateConflictingDirectory)
{
    const auto root = TestRoot() / "tar_single_file_root_option";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    WriteText(root / "source.txt", "PAYLOAD");
    for (const bool include_root : {false, true})
    {
        fsx::ArchiveOptions create;
        create.include_root_directory = include_root;
        const auto archive = root / (include_root ? "with-root.tar" : "without-root.tar");
        const auto created = fsx::CreateArchive((root / "source.txt").string(), archive.string(), create);
        ASSERT_TRUE(created.ok) << created.error;
        for (const auto policy : {fsx::ConflictPolicy::Fail, fsx::ConflictPolicy::Skip, fsx::ConflictPolicy::Overwrite})
        {
            fsx::ArchiveOptions extract;
            extract.conflict_policy = policy;
            const auto out = root / (std::string(include_root ? "with-" : "without-") + fsx::ToString(policy));
            const auto extracted = fsx::ExtractArchive(archive.string(), out.string(), extract);
            ASSERT_TRUE(extracted.ok) << extracted.error;
            ASSERT_TRUE(std::filesystem::is_regular_file(out / "source.txt"));
            EXPECT_EQ(ReadText(out / "source.txt"), "PAYLOAD");
        }
    }
    std::filesystem::remove_all(root, ec);
}

TEST(FsxTests, ArchiveRejectsSourceOutputOverlapBeforeCreatingAnything)
{
    const auto root = TestRoot() / "tar_source_overlap";
    const auto src = root / "src";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    WriteText(src / "a.txt", "SOURCE-SENTINEL");

    for (const auto& source : {src, src / "a.txt"})
    {
        const auto result = fsx::CreateArchive(source.string(), (src / "a.txt").string());
        EXPECT_FALSE(result.ok);
        EXPECT_NE(result.error.find("overlaps"), std::string::npos) << result.error;
        EXPECT_EQ(ReadText(src / "a.txt"), "SOURCE-SENTINEL");
    }
    const auto nested_archive = src / "not-created" / "bundle.tar";
    EXPECT_FALSE(fsx::CreateArchive(src.string(), nested_archive.string()).ok);
    EXPECT_FALSE(std::filesystem::exists(src / "not-created"));
    EXPECT_EQ(ReadText(src / "a.txt"), "SOURCE-SENTINEL");

    ASSERT_TRUE(fsx::CreateArchive(src.string(), (root / "outside" / "bundle.tar").string()).ok);
    ASSERT_TRUE(fsx::ExtractArchive((root / "outside" / "bundle.tar").string(), (root / "unpacked").string()).ok);
    EXPECT_EQ(ReadText(root / "unpacked" / "a.txt"), "SOURCE-SENTINEL");
    EXPECT_EQ(ReadText(src / "a.txt"), "SOURCE-SENTINEL");
    std::filesystem::remove_all(root, ec);
}

TEST(FsxTests, ArchiveRejectsExternalHardLinkToSourceMember)
{
    const auto root = TestRoot() / "tar_source_hardlink";
    const auto src = root / "src";
    const auto archive = root / "alias.tar";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    WriteText(src / "a.txt", "SOURCE-SENTINEL");
    std::filesystem::create_hard_link(src / "a.txt", archive, ec);
    if (ec)
        GTEST_SKIP() << "hard links unavailable: " << ec.message();
    for (const auto& source : {src, src / "a.txt"})
    {
        EXPECT_FALSE(fsx::CreateArchive(source.string(), archive.string()).ok);
        EXPECT_EQ(ReadText(archive), "SOURCE-SENTINEL");
        EXPECT_EQ(ReadText(src / "a.txt"), "SOURCE-SENTINEL");
    }
    std::filesystem::remove_all(root, ec);
}

#if !defined(_WIN32)
TEST(FsxTests, ArchiveRejectsDanglingOutputLinkBeforeCreatingSourceMember)
{
    const auto root = TestRoot() / "tar_dangling_output";
    const auto src = root / "src";
    const auto output = root / "output.tar";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    WriteText(src / "a.txt", "SOURCE-SENTINEL");
    std::filesystem::create_symlink(src / "new.tar", output, ec);
    ASSERT_FALSE(ec) << ec.message();

    const auto result = fsx::CreateArchive(src.string(), output.string());
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error.find("link"), std::string::npos) << result.error;
    EXPECT_FALSE(std::filesystem::exists(src / "new.tar"));
    EXPECT_TRUE(std::filesystem::is_symlink(output));
    EXPECT_EQ(ReadText(src / "a.txt"), "SOURCE-SENTINEL");
    std::filesystem::remove_all(root, ec);
}

TEST(FsxTests, ArchiveRejectsDanglingOutputParentWithoutCreatingDirectories)
{
    const auto root = TestRoot() / "tar_dangling_parent";
    const auto src = root / "src";
    const auto link = root / "linked-parent";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    WriteText(src / "a.txt", "SOURCE-SENTINEL");
    std::filesystem::create_directory_symlink(src / "not-created", link, ec);
    ASSERT_FALSE(ec) << ec.message();

    EXPECT_FALSE(fsx::CreateArchive(src.string(), (link / "nested" / "bundle.tar").string()).ok);
    EXPECT_FALSE(std::filesystem::exists(src / "not-created"));
    EXPECT_TRUE(std::filesystem::is_symlink(link));
    EXPECT_EQ(ReadText(src / "a.txt"), "SOURCE-SENTINEL");

    const auto ordinary = root / "ordinary" / "bundle.tar";
    ASSERT_TRUE(fsx::CreateArchive(src.string(), ordinary.string()).ok);
    ASSERT_TRUE(fsx::ExtractArchive(ordinary.string(), (root / "unpacked").string()).ok);
    EXPECT_EQ(ReadText(root / "unpacked" / "a.txt"), "SOURCE-SENTINEL");
    std::filesystem::remove_all(root, ec);
}
#endif

TEST(FsxTests, ExtractArchiveConflictPoliciesConsumeSkippedPayloadAndPadding)
{
    const auto root = TestRoot() / "tar_skip_payload";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root);
    const auto archive = root / "input.tar";
    const std::string payload(702, 'A');
    {
        std::ofstream out(archive, std::ios::binary);
        WriteTarEntry(out, "a.txt", payload);
        WriteTarEntry(out, "b.txt", "SECOND");
        std::array<char, 1024> zeros{};
        out.write(zeros.data(), static_cast<std::streamsize>(zeros.size()));
    }
    for (const auto policy : {fsx::ConflictPolicy::Skip, fsx::ConflictPolicy::Fail, fsx::ConflictPolicy::Overwrite})
    {
        for (const bool existing_directory : {false, true})
        {
            const auto dest = root / (std::string(fsx::ToString(policy)) + (existing_directory ? "_dir" : "_file"));
            if (existing_directory)
                WriteText(dest / "a.txt" / "keep", "OLD");
            else
                WriteText(dest / "a.txt", "OLD");
            fsx::ArchiveOptions options;
            options.conflict_policy = policy;
            const auto result = fsx::ExtractArchive(archive.string(), dest.string(), options);
            EXPECT_EQ(result.ok, policy != fsx::ConflictPolicy::Fail) << result.error;
            if (policy == fsx::ConflictPolicy::Overwrite)
                EXPECT_EQ(ReadText(dest / "a.txt"), payload);
            else
                EXPECT_EQ(ReadText(existing_directory ? dest / "a.txt" / "keep" : dest / "a.txt"), "OLD");
            if (policy == fsx::ConflictPolicy::Fail)
                EXPECT_FALSE(std::filesystem::exists(dest / "b.txt"));
            else
                EXPECT_EQ(ReadText(dest / "b.txt"), "SECOND");
        }
    }
    std::filesystem::remove_all(root, ec);
}

TEST(FsxTests, ExtractArchiveRejectsDanglingFileLinksForEveryPolicy)
{
    const auto root = TestRoot() / "tar_dangling_entry";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    WriteText(root / "source" / "file", "PAYLOAD");
    const auto archive = root / "input.tar";
    ASSERT_TRUE(fsx::CreateArchive((root / "source").string(), archive.string()).ok);
    for (const auto policy : {fsx::ConflictPolicy::Fail, fsx::ConflictPolicy::Skip, fsx::ConflictPolicy::Overwrite})
    {
        const auto out = root / fsx::ToString(policy);
        const auto outside = root / (std::string("outside-") + fsx::ToString(policy));
        std::filesystem::create_directories(out);
        std::filesystem::create_symlink(outside, out / "file", ec);
        if (ec)
            GTEST_SKIP() << "symbolic links unavailable: " << ec.message();
        fsx::ArchiveOptions options;
        options.conflict_policy = policy;
        const auto result = fsx::ExtractArchive(archive.string(), out.string(), options);
        EXPECT_FALSE(result.ok);
        EXPECT_FALSE(std::filesystem::exists(outside));
        EXPECT_TRUE(std::filesystem::is_symlink(out / "file"));
    }
    ASSERT_TRUE(fsx::ExtractArchive(archive.string(), (root / "ordinary").string()).ok);
    EXPECT_EQ(ReadText(root / "ordinary" / "file"), "PAYLOAD");
    std::filesystem::remove_all(root, ec);
}

TEST(FsxTests, ExtractArchiveRejectsDirectoryLinksAndLinkedParents)
{
    const auto root = TestRoot() / "tar_linked_directory_entry";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    WriteText(root / "source" / "directory" / "file", "PAYLOAD");
    const auto archive = root / "input.tar";
    ASSERT_TRUE(fsx::CreateArchive((root / "source").string(), archive.string()).ok);
    for (const bool dangling : {false, true})
    {
        for (const bool directory_entry : {false, true})
        {
            const auto out =
                root / (std::string(dangling ? "dangling-" : "existing-") + (directory_entry ? "entry" : "parent"));
            const auto linked = out / "inside";
            std::filesystem::create_directories(out);
            if (!dangling)
                std::filesystem::create_directories(linked);
            std::filesystem::create_directory_symlink(linked, out / "directory", ec);
            if (ec)
                GTEST_SKIP() << "directory symlinks unavailable: " << ec.message();
            if (!directory_entry)
                WriteTarWithSingleEntry(archive, "directory/file", "PAYLOAD");
            else
                ASSERT_TRUE(fsx::CreateArchive((root / "source").string(), archive.string()).ok);
            const auto result = fsx::ExtractArchive(archive.string(), out.string());
            EXPECT_FALSE(result.ok);
            EXPECT_TRUE(std::filesystem::is_symlink(out / "directory"));
            EXPECT_FALSE(std::filesystem::exists(linked / "file"));
            EXPECT_EQ(std::filesystem::exists(linked), !dangling);
        }
    }
    std::filesystem::remove_all(root, ec);
}

TEST(FsxTests, ExtractArchiveDirectoryTypeConflictsFollowPolicy)
{
    const auto root = TestRoot() / "tar_skip_directory";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root);
    const auto archive = root / "input.tar";
    {
        std::ofstream out(archive, std::ios::binary);
        WriteTarEntry(out, "dir/", "", true);
        WriteTarEntry(out, "dir/child", std::string(702, 'C'));
        WriteTarEntry(out, "b.txt", "SECOND");
        std::array<char, 1024> zeros{};
        out.write(zeros.data(), static_cast<std::streamsize>(zeros.size()));
    }
    for (const auto policy : {fsx::ConflictPolicy::Skip, fsx::ConflictPolicy::Fail, fsx::ConflictPolicy::Overwrite})
    {
        const auto dest = root / fsx::ToString(policy);
        WriteText(dest / "dir", "OLD FILE");
        fsx::ArchiveOptions options;
        options.conflict_policy = policy;
        const auto result = fsx::ExtractArchive(archive.string(), dest.string(), options);
        EXPECT_EQ(result.ok, policy != fsx::ConflictPolicy::Fail) << result.error;
        if (policy == fsx::ConflictPolicy::Overwrite)
            EXPECT_EQ(ReadText(dest / "dir" / "child"), std::string(702, 'C'));
        else
            EXPECT_EQ(ReadText(dest / "dir"), "OLD FILE");
        if (policy == fsx::ConflictPolicy::Fail)
            EXPECT_FALSE(std::filesystem::exists(dest / "b.txt"));
        else
            EXPECT_EQ(ReadText(dest / "b.txt"), "SECOND");
    }
    const auto merge = root / "existing-dir";
    WriteText(merge / "dir" / "keep", "KEEP");
    fsx::ArchiveOptions skip;
    skip.conflict_policy = fsx::ConflictPolicy::Skip;
    EXPECT_TRUE(fsx::ExtractArchive(archive.string(), merge.string(), skip).ok);
    EXPECT_EQ(ReadText(merge / "dir" / "keep"), "KEEP");
    EXPECT_EQ(ReadText(merge / "dir" / "child"), std::string(702, 'C'));
    std::filesystem::remove_all(root, ec);
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
