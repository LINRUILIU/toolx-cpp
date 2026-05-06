#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <thread>

#include "utils.h"

TEST(UtilsStringTests, TrimFamilyWorks)
{
    EXPECT_EQ(utils::str::ltrim("  abc  "), "abc  ");
    EXPECT_EQ(utils::str::rtrim("  abc  \n"), "  abc");
    EXPECT_EQ(utils::str::trim("\t abc \r\n"), "abc");
}

TEST(UtilsStringTests, SplitDefaultKeepsEmpty)
{
    const auto parts = utils::str::split("a,,b,", ',', false);
    ASSERT_EQ(parts.size(), 4U);
    EXPECT_EQ(parts[0], "a");
    EXPECT_EQ(parts[1], "");
    EXPECT_EQ(parts[2], "b");
    EXPECT_EQ(parts[3], "");
}

TEST(UtilsStringTests, SplitSkipEmptyWorks)
{
    const auto parts = utils::str::split("a,,b,", ',', true);
    ASSERT_EQ(parts.size(), 2U);
    EXPECT_EQ(parts[0], "a");
    EXPECT_EQ(parts[1], "b");
}

TEST(UtilsStringTests, LowerIequalsAndAffix)
{
    EXPECT_EQ(utils::str::to_lower_ascii("AbC-12"), "abc-12");
    EXPECT_TRUE(utils::str::iequals("AbC", "aBc"));
    EXPECT_FALSE(utils::str::iequals("AbC", "AbCD"));
    EXPECT_TRUE(utils::str::starts_with("alpha-beta", "alpha"));
    EXPECT_TRUE(utils::str::ends_with("alpha-beta", "beta"));
}

TEST(UtilsTimeTests, NowAndFormatWork)
{
    const auto now = utils::time::now_system_ms();
    EXPECT_GT(now, 0);

    const auto text = utils::time::format_local_timestamp_ms(now);
    EXPECT_EQ(text.size(), 23U);
    EXPECT_EQ(text[4], '-');
    EXPECT_EQ(text[7], '-');
    EXPECT_EQ(text[10], ' ');
    EXPECT_EQ(text[13], ':');
    EXPECT_EQ(text[16], ':');
    EXPECT_EQ(text[19], '.');
}

TEST(UtilsTimeTests, NegativeEpochFormattingKeepsMillisecondPart)
{
    const auto text = utils::time::format_local_timestamp_ms(-1);
    EXPECT_EQ(text.size(), 23U);
    EXPECT_EQ(text[4], '-');
    EXPECT_EQ(text[7], '-');
    EXPECT_EQ(text[10], ' ');
    EXPECT_EQ(text[13], ':');
    EXPECT_EQ(text[16], ':');
    EXPECT_EQ(text[19], '.');
}

TEST(UtilsTimeTests, SteadyElapsedIsNonNegative)
{
    const auto start = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    const auto elapsed = utils::time::steady_elapsed_ms(start);
    EXPECT_GE(elapsed, 0);
}

TEST(UtilsParseTests, ParseInt32)
{
    const auto ok = utils::parse::parse_int32("-123");
    ASSERT_TRUE(ok.ok);
    EXPECT_EQ(ok.value, -123);

    const auto bad = utils::parse::parse_int32("12x");
    ASSERT_FALSE(bad.ok);
    EXPECT_NE(bad.error.find("[scope=parse] [key=int32]"), std::string::npos);

    const auto empty = utils::parse::parse_int32("   ");
    ASSERT_FALSE(empty.ok);
    EXPECT_NE(empty.error.find("empty input"), std::string::npos);

    const auto overflow = utils::parse::parse_int32("9999999999999");
    ASSERT_FALSE(overflow.ok);
    EXPECT_NE(overflow.error.find("out of range"), std::string::npos);
}

TEST(UtilsParseTests, ParseDouble)
{
    const auto ok = utils::parse::parse_double("3.5");
    ASSERT_TRUE(ok.ok);
    EXPECT_DOUBLE_EQ(ok.value, 3.5);

    const auto inf = utils::parse::parse_double("inf");
    ASSERT_FALSE(inf.ok);
    EXPECT_NE(inf.error.find("non-finite"), std::string::npos);

    const auto empty = utils::parse::parse_double(" ");
    ASSERT_FALSE(empty.ok);
    EXPECT_NE(empty.error.find("empty input"), std::string::npos);

    const auto overflow = utils::parse::parse_double("1e309");
    ASSERT_FALSE(overflow.ok);
    EXPECT_NE(overflow.error.find("out of range"), std::string::npos);
}

TEST(UtilsParseTests, ParseBool)
{
    const auto t = utils::parse::parse_bool(" YES ");
    ASSERT_TRUE(t.ok);
    EXPECT_TRUE(t.value);

    const auto f = utils::parse::parse_bool("off");
    ASSERT_TRUE(f.ok);
    EXPECT_FALSE(f.value);

    const auto bad = utils::parse::parse_bool("maybe");
    ASSERT_FALSE(bad.ok);
    EXPECT_NE(bad.error.find("[scope=parse] [key=bool]"), std::string::npos);
}

TEST(UtilsErrTests, JoinContextFormat)
{
    const auto msg = utils::err::join_context("parse", "int32", "invalid");
    EXPECT_EQ(msg, "[scope=parse] [key=int32] invalid");
    EXPECT_EQ(utils::err::format_error("a", "b", "c"), "[scope=a] [key=b] c");
}

TEST(UtilsPathTests, NormalizeSlashAndFileExists)
{
    EXPECT_EQ(utils::path::normalize_slash("a\\b\\c"), "a/b/c");

    const auto temp = std::filesystem::current_path() / "toolx_test_tmp" / "utils_exists_test.txt";
    std::filesystem::create_directories(temp.parent_path());
    {
        std::ofstream out(temp.string(), std::ios::binary);
        out << "ok";
    }

    EXPECT_TRUE(utils::path::file_exists(temp.string()));
    EXPECT_FALSE(utils::path::file_exists(temp.parent_path().string()));
    EXPECT_FALSE(utils::path::file_exists("   "));
    std::error_code ec;
    std::filesystem::remove(temp, ec);
}

TEST(UtilsPathTests, EnsureParentDir)
{
    const auto root = std::filesystem::current_path() / "toolx_test_tmp" / "utils_parent_dir_test";
    const auto file = root / "a" / "b" / "c.txt";

    std::error_code ec;
    std::filesystem::remove_all(root, ec);

    const auto st = utils::path::ensure_parent_dir(file.string());
    ASSERT_TRUE(st.ok);
    EXPECT_TRUE(std::filesystem::exists(file.parent_path()));

    const auto empty = utils::path::ensure_parent_dir(" ");
    ASSERT_FALSE(empty.ok);
    EXPECT_NE(empty.error.find("empty file path"), std::string::npos);

    std::filesystem::remove_all(root, ec);
}

TEST(UtilsStringTests, MeasureTextStrlenSizeof)
{
    EXPECT_EQ(utils::str::measure_text_utf8_strlen("abc"), 3U);
}

TEST(UtilsStringTests, MeasureTextStrlenStopsAtNull)
{
    const std::string s{"ab\0cd", 5};
    EXPECT_EQ(utils::str::measure_text_utf8_strlen(s), 2U);
}

TEST(UtilsStringTests, MeasureTextCodepointsAndDisplayWidth)
{
    const std::string text = std::string("ab") + "\xE4\xB8\xAD";
    EXPECT_EQ(utils::str::measure_text_utf8_codepoints(text), 3U);
    EXPECT_EQ(utils::str::measure_text_utf8_display_width(text), 4U);
}

TEST(UtilsStringTests, MeasureTextInvalidUtf8FallsBackPerByte)
{
    const std::string broken = std::string("A") + static_cast<char>(0xE4) + "B";
    EXPECT_EQ(utils::str::measure_text_utf8_codepoints(broken), 3U);
    EXPECT_EQ(utils::str::measure_text_utf8_display_width(broken), 3U);
}

TEST(UtilsStringTests, MeasureTextGbkStrlenAndMetrics)
{
    const std::string gbk_text = std::string("A") + "\xD6\xD0" + "B";
    EXPECT_EQ(utils::str::measure_text_gbk_strlen(gbk_text), 4U);
    EXPECT_EQ(utils::str::measure_text_gbk_codepoints(gbk_text), 3U);
    EXPECT_EQ(utils::str::measure_text_gbk_display_width(gbk_text), 4U);
}

TEST(UtilsStringTests, MeasureTextGbkInvalidTrailFallsBackByteWise)
{
    const std::string broken = std::string("A") + "\x81\x30" + "B";
    EXPECT_EQ(utils::str::measure_text_gbk_codepoints(broken), 4U);
    EXPECT_EQ(utils::str::measure_text_gbk_display_width(broken), 4U);
}
