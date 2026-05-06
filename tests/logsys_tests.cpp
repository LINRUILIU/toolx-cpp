#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#include "logsys.h"

TEST(ErrorCodeTests, BuildAndParse)
{
    const auto code = LOGSYS_MAKE_ERROR_CODE(logsys::ErrorSource::System, logsys::ModuleId::Core, 0x1234);

    EXPECT_EQ(LOGSYS_ERROR_SOURCE(code), static_cast<std::uint8_t>(logsys::ErrorSource::System));
    EXPECT_EQ(LOGSYS_ERROR_MODULE(code), static_cast<std::uint8_t>(logsys::ModuleId::Core));
    EXPECT_EQ(LOGSYS_ERROR_DETAIL(code), 0x1234);
}

namespace
{
    std::filesystem::path TestTempPath(std::string_view name)
    {
        return std::filesystem::current_path() / "toolx_test_tmp" / std::string(name);
    }

    class MemorySink final : public logsys::ISink
    {
    public:
        void Write(const std::string &line) override
        {
            lines.push_back(line);
        }

        void Flush() override {}

        std::vector<std::string> lines;
    };
} // namespace

TEST(LoggerTests, CounterByCodeAndCategory)
{
    using namespace logsys;

    auto &logger = Logger::Instance();
#if defined(_WIN32)
    _putenv("LOGSYS_ALLOW_TEST_API=1");
#else
    setenv("LOGSYS_ALLOW_TEST_API", "1", 1);
#endif
    ASSERT_TRUE(logger.ResetErrorCountersForTestOnly());
    logger.SetLevel(LogLevel::Trace);

    auto sink = std::make_shared<MemorySink>();
    logger.AddDefaultSink(sink);

    const auto code = LOGSYS_MAKE_ERROR_CODE(ErrorSource::Business, ModuleId::BusinessCommon, 7);
    logger.Logf(LogLevel::Error, code, ErrorCategory::Business, __FILE__, __LINE__, __func__, 0, "", "", "hello %d", 1);
    logger.Flush();

    EXPECT_EQ(logger.GetErrorCountByCode(code), 1u);
    EXPECT_EQ(logger.GetErrorCountByCategory(ErrorCategory::Business), 1u);
    ASSERT_FALSE(sink->lines.empty());
}

TEST(LoggerTests, SimpleDefaultApiUsesConfiguredOrigin)
{
    using namespace logsys;

    auto &logger = Logger::Instance();
#if defined(_WIN32)
    _putenv("LOGSYS_ALLOW_TEST_API=1");
#else
    setenv("LOGSYS_ALLOW_TEST_API", "1", 1);
#endif
    ASSERT_TRUE(logger.ResetErrorCountersForTestOnly());

    DefaultLoggerOptions options;
    options.level = LogLevel::Trace;
    options.enable_console = false;
    options.enable_file = false;
    options.enable_debugger = false;
    logger.ConfigureDefaultLogger(options);

    auto sink = std::make_shared<MemorySink>();
    logger.AddDefaultSink(sink);
    logger.SetDefaultOrigin(ErrorSource::Business, ModuleId::BusinessCommon, ErrorCategory::Business);

    logger.LogDefaultf(LogLevel::Info, __FILE__, __LINE__, __func__, "simple-api %d", 7);
    logger.Flush();

    const auto expected = LOGSYS_MAKE_ERROR_CODE(ErrorSource::Business, ModuleId::BusinessCommon, 3);
    EXPECT_EQ(logger.GetErrorCountByCode(expected), 1u);
    EXPECT_EQ(logger.GetErrorCountByCategory(ErrorCategory::Business), 1u);
    ASSERT_FALSE(sink->lines.empty());
}

TEST(LoggerTests, TextFieldMaskCanHideTimestampAndCode)
{
    using namespace logsys;

    auto &logger = Logger::Instance();
    DefaultLoggerOptions options;
    options.level = LogLevel::Trace;
    options.enable_console = false;
    options.enable_file = false;
    options.enable_debugger = false;
    logger.ConfigureDefaultLogger(options);

    auto sink = std::make_shared<MemorySink>();
    logger.AddDefaultSink(sink);

    logger.SetTextFieldMask(kTextFieldMaskDefault);
    logger.SetTextFieldEnabled(TextField::Timestamp, false);
    logger.SetTextFieldEnabled(TextField::Code, false);

    logger.LogDefaultf(LogLevel::Info, __FILE__, __LINE__, __func__, "field-mask-test");
    logger.Flush();

    ASSERT_FALSE(sink->lines.empty());
    const std::string line = sink->lines.back();
    const auto code_str = std::to_string(logger.DefaultCodeForLevel(LogLevel::Info));
    EXPECT_NE(line.find("INFO"), std::string::npos);
    EXPECT_EQ(line.find("INFO " + code_str + " "), std::string::npos);
}

TEST(LoggerTests, AutoFillMissingMetadataForManualEvent)
{
    using namespace logsys;

    auto &logger = Logger::Instance();
    DefaultLoggerOptions options;
    options.level = LogLevel::Trace;
    options.enable_console = false;
    options.enable_file = false;
    options.enable_debugger = false;
    logger.ConfigureDefaultLogger(options);

    auto sink = std::make_shared<MemorySink>();
    logger.AddDefaultSink(sink);

    logger.SetAutoFillMissingMetadata(true);
    logger.SetDefaultOrigin(ErrorSource::Business, ModuleId::BusinessCommon, ErrorCategory::Business);

    LogEvent event;
    event.level = LogLevel::Warning;
    logger.LogEventNow(std::move(event));

    ASSERT_FALSE(sink->lines.empty());
    const std::string line = sink->lines.back();
    EXPECT_NE(line.find("<unknown>"), std::string::npos);
    EXPECT_NE(line.find("<empty>"), std::string::npos);
}

TEST(LoggerTests, SimpleLoggerDefaultsToMessageAndLevel)
{
    using namespace logsys;

    auto &logger = Logger::Instance();
    logger.ConfigureSimpleLogger(LogLevel::Info, false, false);

    auto sink = std::make_shared<MemorySink>();
    logger.AddDefaultSink(sink);

    logger.LogDefaultf(LogLevel::Info, __FILE__, __LINE__, __func__, "mini-mode");
    logger.Flush();

    ASSERT_FALSE(sink->lines.empty());
    const std::string line = sink->lines.back();
    EXPECT_NE(line.find("INFO"), std::string::npos);
    EXPECT_NE(line.find("mini-mode"), std::string::npos);
    EXPECT_EQ(line.find(__FILE__), std::string::npos);
}

TEST(LoggerTests, SetLevelFromArgsAndString)
{
    using namespace logsys;

    auto &logger = Logger::Instance();
    logger.ConfigureSimpleLogger();

    const char *argv1[] = {"app", "--log-level=debug"};
    ASSERT_TRUE(logger.SetLevelFromArgs(2, argv1));
    EXPECT_EQ(logger.Level(), LogLevel::Debug);

    const char *argv2[] = {"app", "--log-level", "info"};
    ASSERT_TRUE(logger.SetLevelFromArgs(3, argv2));
    EXPECT_EQ(logger.Level(), LogLevel::Info);

    EXPECT_TRUE(logger.SetLevelFromString("fatal"));
    EXPECT_EQ(logger.Level(), LogLevel::Fatal);
    EXPECT_FALSE(logger.SetLevelFromString("bad-level"));
}

TEST(LoggerTests, DefaultSimpleModeRecordsInfoButOutputsFatal)
{
    using namespace logsys;

    auto &logger = Logger::Instance();
#if defined(_WIN32)
    _putenv("LOGSYS_ALLOW_TEST_API=1");
#else
    setenv("LOGSYS_ALLOW_TEST_API", "1", 1);
#endif
    ASSERT_TRUE(logger.ResetErrorCountersForTestOnly());

    logger.ConfigureSimpleLogger();
    auto sink = std::make_shared<MemorySink>();
    logger.AddDefaultSink(sink);

    logger.LogDefaultf(LogLevel::Info, __FILE__, __LINE__, __func__, "info-stored");
    logger.LogDefaultf(LogLevel::Critical, __FILE__, __LINE__, __func__, "critical-output");
    logger.Flush();

    const auto info_code = logger.DefaultCodeForLevel(LogLevel::Info);
    EXPECT_EQ(logger.RecordLevel(), LogLevel::Info);
    EXPECT_EQ(logger.Level(), LogLevel::Fatal);
    EXPECT_EQ(logger.GetErrorCountByCode(info_code), 1u);
    ASSERT_FALSE(sink->lines.empty());
    EXPECT_NE(sink->lines.back().find("critical-output"), std::string::npos);
}

TEST(LoggerTests, ProfileResolverFilePathOverridesModule)
{
    using namespace logsys;

    LoggerConfigV2 cfg;
    cfg.global_record_level = LogLevel::Info;
    cfg.global_output_level = LogLevel::Fatal;
    cfg.global_text_field_mask = kTextFieldMaskSimple;

    ProfileConfigV2 module_profile;
    module_profile.name = "module-default";
    module_profile.module = ModuleId::BusinessCommon;
    module_profile.output_level = LogLevel::Warning;

    ProfileConfigV2 file_profile;
    file_profile.name = "file-override";
    file_profile.file_path_pattern = "*special.cpp";
    file_profile.output_level = LogLevel::Debug;

    cfg.profiles.push_back(module_profile);
    cfg.profiles.push_back(file_profile);

    const auto resolved = ProfileResolverV2::Resolve(cfg, "src/game/special.cpp", ModuleId::BusinessCommon);
    EXPECT_EQ(resolved.output_level, LogLevel::Debug);
}

TEST(LoggerTests, ApplyConfigV2UpdatesThresholds)
{
    using namespace logsys;

    auto &logger = Logger::Instance();

    LoggerConfigV2 cfg;
    cfg.global_record_level = LogLevel::Debug;
    cfg.global_output_level = LogLevel::Error;
    cfg.global_text_field_mask = static_cast<std::uint32_t>(TextField::Level) | static_cast<std::uint32_t>(TextField::Message);
    cfg.global_enable_console = false;
    cfg.global_enable_file = false;
    cfg.global_enable_debugger = false;
    cfg.schedule.periodic_flush_enabled = false;

    logger.ApplyConfigV2(cfg);

    EXPECT_EQ(logger.RecordLevel(), LogLevel::Debug);
    EXPECT_EQ(logger.Level(), LogLevel::Error);
    EXPECT_EQ(logger.TextFieldMask(), cfg.global_text_field_mask);
}

TEST(LoggerTests, PeriodicFlushLifecycle)
{
    using namespace logsys;

    auto &logger = Logger::Instance();
    logger.StartPeriodicFlush(std::chrono::milliseconds(200));
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    logger.StopPeriodicFlush();

    SUCCEED();
}

TEST(LoggerTests, LoadConfigV2FromJsonFileAppliesSettings)
{
    using namespace logsys;

    auto &logger = Logger::Instance();
    const auto cfg_path = TestTempPath("logsys_v2_test.json");
    std::filesystem::create_directories(cfg_path.parent_path());

    std::ofstream out(cfg_path.string(), std::ios::trunc);
    out << R"({
  "global_record_level": "debug",
  "global_output_level": "error",
  "global_text_field_mask": 66,
  "global_enable_console": false,
  "global_enable_file": false,
  "global_enable_debugger": false,
  "output_order": "by_time_mixed",
  "schedule": {
    "periodic_flush_enabled": false,
    "flush_interval_ms": 1000
  },
  "backpressure": {
    "drop_low_level_when_full": true,
    "drop_below_level": "info",
    "queue_high_watermark": 5
  }
})";
    out.close();

    ASSERT_TRUE(logger.LoadConfigV2FromJsonFile(cfg_path.string()));
    EXPECT_EQ(logger.RecordLevel(), LogLevel::Debug);
    EXPECT_EQ(logger.Level(), LogLevel::Error);
    EXPECT_EQ(logger.TextFieldMask(), 66u);

    const auto cfg = logger.CurrentConfigV2();
    EXPECT_EQ(cfg.backpressure.queue_high_watermark, 5u);
    EXPECT_EQ(cfg.backpressure.drop_below_level, LogLevel::Info);

    std::error_code ec;
    std::filesystem::remove(cfg_path, ec);
}

TEST(LoggerTests, BackpressureDropsLowLevelEvents)
{
    using namespace logsys;

    auto &logger = Logger::Instance();
#if defined(_WIN32)
    _putenv("LOGSYS_ALLOW_TEST_API=1");
#else
    setenv("LOGSYS_ALLOW_TEST_API", "1", 1);
#endif
    ASSERT_TRUE(logger.ResetErrorCountersForTestOnly());
    logger.ResetBackpressureCountersForTestOnly();

    LoggerConfigV2 cfg;
    cfg.global_record_level = LogLevel::Trace;
    cfg.global_output_level = LogLevel::Fatal;
    cfg.global_enable_console = false;
    cfg.global_enable_file = false;
    cfg.global_enable_debugger = false;
    cfg.schedule.periodic_flush_enabled = false;
    cfg.backpressure.drop_low_level_when_full = true;
    cfg.backpressure.drop_below_level = LogLevel::Info;
    cfg.backpressure.queue_high_watermark = 0;
    logger.ApplyConfigV2(cfg);

    logger.LogDefaultf(LogLevel::Debug, __FILE__, __LINE__, __func__, "debug-dropped");
    EXPECT_EQ(logger.DroppedByBackpressureCount(), 1u);

    logger.LogDefaultf(LogLevel::Info, __FILE__, __LINE__, __func__, "info-kept");
    logger.Flush();
    const auto info_code = logger.DefaultCodeForLevel(LogLevel::Info);
    EXPECT_EQ(logger.GetErrorCountByCode(info_code), 1u);
}

TEST(LoggerTests, OutputOrderGroupedFlushesByLevel)
{
    using namespace logsys;

    auto &logger = Logger::Instance();
    LoggerConfigV2 cfg;
    cfg.global_record_level = LogLevel::Trace;
    cfg.global_output_level = LogLevel::Trace;
    cfg.global_enable_console = false;
    cfg.global_enable_file = false;
    cfg.global_enable_debugger = false;
    cfg.schedule.periodic_flush_enabled = false;
    cfg.output_order = OutputOrderMode::ByLevelGrouped;
    logger.ApplyConfigV2(cfg);

    auto sink = std::make_shared<MemorySink>();
    logger.AddDefaultSink(sink);

    logger.LogDefaultf(LogLevel::Error, __FILE__, __LINE__, __func__, "err-first");
    logger.LogDefaultf(LogLevel::Info, __FILE__, __LINE__, __func__, "info-second");
    logger.Flush();

    ASSERT_EQ(sink->lines.size(), 2u);
    EXPECT_NE(sink->lines[0].find("INFO"), std::string::npos);
    EXPECT_NE(sink->lines[1].find("ERROR"), std::string::npos);
}

TEST(LoggerTests, FlushDrainsAsyncQueueIntoSink)
{
    using namespace logsys;

    auto &logger = Logger::Instance();
    LoggerConfigV2 cfg;
    cfg.global_record_level = LogLevel::Trace;
    cfg.global_output_level = LogLevel::Trace;
    cfg.global_enable_console = false;
    cfg.global_enable_file = false;
    cfg.global_enable_debugger = false;
    cfg.schedule.periodic_flush_enabled = false;
    logger.ApplyConfigV2(cfg);

    auto sink = std::make_shared<MemorySink>();
    logger.AddDefaultSink(sink);

    for (int i = 0; i < 5; ++i)
    {
        logger.LogDefaultf(LogLevel::Info, __FILE__, __LINE__, __func__, "queued-%d", i);
    }

    logger.Flush();

    ASSERT_EQ(sink->lines.size(), 5u);
    EXPECT_NE(sink->lines.back().find("queued-4"), std::string::npos);
}

TEST(LoggerTests, FatalFlushOnlyPolicyDoesNotAbort)
{
    using namespace logsys;

    auto &logger = Logger::Instance();
    LoggerConfigV2 cfg;
    cfg.global_record_level = LogLevel::Trace;
    cfg.global_output_level = LogLevel::Trace;
    cfg.global_enable_console = false;
    cfg.global_enable_file = false;
    cfg.global_enable_debugger = false;
    cfg.schedule.periodic_flush_enabled = false;
    cfg.fatal_policy = FatalPolicy::FlushOnly;
    logger.ApplyConfigV2(cfg);

    auto sink = std::make_shared<MemorySink>();
    logger.AddDefaultSink(sink);

    logger.LogDefaultf(LogLevel::Fatal, __FILE__, __LINE__, __func__, "fatal-but-library-stays-alive");
    logger.Flush();

    ASSERT_EQ(sink->lines.size(), 1u);
    EXPECT_NE(sink->lines.front().find("fatal-but-library-stays-alive"), std::string::npos);
    EXPECT_EQ(logger.GetFatalPolicy(), FatalPolicy::FlushOnly);
}

TEST(LoggerTests, ReapplyingConfigCanDisablePeriodicFlush)
{
    using namespace logsys;

    auto &logger = Logger::Instance();

    LoggerConfigV2 enabled;
    enabled.global_record_level = LogLevel::Info;
    enabled.global_output_level = LogLevel::Fatal;
    enabled.global_enable_console = false;
    enabled.global_enable_file = false;
    enabled.global_enable_debugger = false;
    enabled.schedule.periodic_flush_enabled = true;
    enabled.schedule.flush_interval = std::chrono::milliseconds(25);
    logger.ApplyConfigV2(enabled);

    LoggerConfigV2 disabled = enabled;
    disabled.schedule.periodic_flush_enabled = false;
    logger.ApplyConfigV2(disabled);

    const auto current = logger.CurrentConfigV2();
    EXPECT_FALSE(current.schedule.periodic_flush_enabled);
    EXPECT_EQ(current.schedule.flush_interval, std::chrono::milliseconds(25));
}

TEST(LoggerTests, JsonProfilesSupportModuleAndFileOverrides)
{
    using namespace logsys;

    auto &logger = Logger::Instance();
    const auto cfg_path = TestTempPath("logsys_v2_profiles_test.json");
    std::filesystem::create_directories(cfg_path.parent_path());

    std::ofstream out(cfg_path.string(), std::ios::trunc);
    out << R"({
    "global_record_level": "info",
    "global_output_level": "fatal",
    "output_order": "by_time_mixed",
    "profiles": [
        {
            "name": "module-default",
            "module": "business_common",
            "output_level": "warning"
        },
        {
            "name": "file-override",
            "file_path_pattern": "*special.cpp",
            "output_level": "debug"
        }
    ]
})";
    out.close();

    ASSERT_TRUE(logger.LoadConfigV2FromJsonFile(cfg_path.string()));

    const auto cfg = logger.CurrentConfigV2();
    ASSERT_EQ(cfg.profiles.size(), 2u);
    const auto resolved = ProfileResolverV2::Resolve(cfg, "src/game/special.cpp", ModuleId::BusinessCommon);
    EXPECT_EQ(resolved.output_level, LogLevel::Debug);

    std::error_code ec;
    std::filesystem::remove(cfg_path, ec);
}
