#include <gtest/gtest.h>

#include <cctype>
#include <string>
#include <vector>

#include "argtool.h"

namespace
{

    class CaptureLogger final : public argtool::IParseLogger
    {
    public:
        void OnError(const argtool::ParseError &error) override
        {
            errors.push_back(error);
        }

        void OnWarning(std::string_view message) override
        {
            warnings.push_back(std::string(message));
        }

        std::vector<argtool::ParseError> errors;
        std::vector<std::string> warnings;
    };

    argtool::Parser BuildParser(CaptureLogger *logger = nullptr)
    {
        argtool::Parser parser;
        parser.SetProgramName("app.exe")
            .SetDescription("argtool tests")
            .SetUsageExample("app.exe -v --output build.log input.txt -- --literal")
            .SetLogger(logger);

        parser.Flag("verbose", 'v')
            .BoolMode(argtool::BoolFlagMode::Count)
            .Description("Enable verbose output.")
            .Done()
            .Option("output", 'o')
            .String()
            .ValueName("FILE")
            .Default("app.log")
            .Description("Output file path.")
            .Done()
            .Option("level", 'l')
            .Int()
            .Range(0, 5)
            .Default("3")
            .Description("Log level in [0,5].")
            .Done()
            .Positional("input")
            .String()
            .Required(true)
            .Description("Primary input file.")
            .Done()
            .Positional("extras")
            .String()
            .Required(false)
            .Variadic(true)
            .Description("Extra positional arguments.")
            .Done();

        return parser;
    }

    std::vector<const char *> ToArgv(const std::vector<std::string> &args)
    {
        std::vector<const char *> argv;
        argv.reserve(args.size());
        for (const auto &arg : args)
        {
            argv.push_back(arg.c_str());
        }
        return argv;
    }

} // namespace

TEST(ArgtoolTests, ParseMixedTokensAndDoubleDash)
{
    auto parser = BuildParser();
    const std::vector<std::string> args = {
        "app.exe", "-vv", "input.txt", "--output", "out.log", "--", "--raw", "tail"};
    const auto argv = ToArgv(args);

    const auto result = parser.Parse(static_cast<int>(argv.size()), argv.data());
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(result.GetCount("verbose"), 2);
    EXPECT_EQ(result.GetString("output"), "out.log");
    EXPECT_EQ(result.GetString("input"), "input.txt");

    const auto extras = result.GetAll("extras");
    ASSERT_EQ(extras.size(), 2U);
    EXPECT_EQ(extras[0], "--raw");
    EXPECT_EQ(extras[1], "tail");
}

TEST(ArgtoolTests, MissingRequiredPositionalHasStructuredError)
{
    auto parser = BuildParser();
    const std::vector<std::string> args = {"app.exe"};
    const auto argv = ToArgv(args);

    const auto result = parser.Parse(static_cast<int>(argv.size()), argv.data());
    ASSERT_FALSE(result.ok);
    ASSERT_TRUE(result.error.has_value());
    EXPECT_EQ(result.error->kind, argtool::ParseErrorKind::MissingRequired);
    EXPECT_EQ(result.error->field, "input");
}

TEST(ArgtoolTests, TypeMismatchErrorIsReported)
{
    auto parser = BuildParser();
    const std::vector<std::string> args = {"app.exe", "--level", "oops", "input.txt"};
    const auto argv = ToArgv(args);

    const auto result = parser.Parse(static_cast<int>(argv.size()), argv.data());
    ASSERT_FALSE(result.ok);
    ASSERT_TRUE(result.error.has_value());
    EXPECT_EQ(result.error->kind, argtool::ParseErrorKind::TypeMismatch);
    EXPECT_EQ(result.error->field, "level");
}

TEST(ArgtoolTests, RangeErrorDefaultPolicyIsFail)
{
    auto parser = BuildParser();
    const std::vector<std::string> args = {"app.exe", "--level", "9", "input.txt"};
    const auto argv = ToArgv(args);

    const auto result = parser.Parse(static_cast<int>(argv.size()), argv.data());
    ASSERT_FALSE(result.ok);
    ASSERT_TRUE(result.error.has_value());
    EXPECT_EQ(result.error->kind, argtool::ParseErrorKind::RangeError);
}

TEST(ArgtoolTests, RangeFallbackUsesDefaultAndWarns)
{
    CaptureLogger logger;
    argtool::Parser parser;
    parser.SetProgramName("app.exe").SetLogger(&logger);
    parser.Option("level", 'l')
        .Int()
        .Range(0, 5, argtool::RangePolicy::UseDefaultAndWarn)
        .Default("3")
        .Description("Level")
        .Done()
        .Positional("input")
        .String()
        .Description("Input")
        .Done();

    const std::vector<std::string> args = {"app.exe", "--level", "9", "input.txt"};
    const auto argv = ToArgv(args);

    const auto result = parser.Parse(static_cast<int>(argv.size()), argv.data());
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.GetInt("level"), 3);
    ASSERT_EQ(logger.warnings.size(), 1U);
    EXPECT_NE(logger.warnings[0].find("fallback to default"), std::string::npos);
}

TEST(ArgtoolTests, ChoiceValidationIsCaseInsensitive)
{
    argtool::Parser parser;
    parser.SetProgramName("app.exe");
    parser.Option("mode", 'm')
        .String()
        .Choices({"Debug", "Release"})
        .Description("Build mode")
        .Done()
        .Positional("input")
        .String()
        .Description("Input")
        .Done();

    const std::vector<std::string> args = {"app.exe", "--mode", "debug", "input.txt"};
    const auto argv = ToArgv(args);

    const auto result = parser.Parse(static_cast<int>(argv.size()), argv.data());
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.GetString("mode"), "Debug");
}

TEST(ArgtoolTests, ChoiceErrorIsReported)
{
    argtool::Parser parser;
    parser.SetProgramName("app.exe");
    parser.Option("mode", 'm')
        .String()
        .Choices({"Debug", "Release"})
        .Done()
        .Positional("input")
        .String()
        .Done();

    const std::vector<std::string> args = {"app.exe", "--mode", "fast", "input.txt"};
    const auto argv = ToArgv(args);

    const auto result = parser.Parse(static_cast<int>(argv.size()), argv.data());
    ASSERT_FALSE(result.ok);
    ASSERT_TRUE(result.error.has_value());
    EXPECT_EQ(result.error->kind, argtool::ParseErrorKind::ChoiceError);
}

TEST(ArgtoolTests, MutexConstraintWorks)
{
    argtool::Parser parser;
    parser.SetProgramName("app.exe");
    parser.Flag("json", 'j').Description("Json").Done().Flag("plain", 'p').Description("Plain").Done().Positional("input").String().Done().AddMutexGroup({{"json", "plain"}, "Use either --json or --plain."});

    const std::vector<std::string> args = {"app.exe", "--json", "--plain", "input.txt"};
    const auto argv = ToArgv(args);

    const auto result = parser.Parse(static_cast<int>(argv.size()), argv.data());
    ASSERT_FALSE(result.ok);
    ASSERT_TRUE(result.error.has_value());
    EXPECT_EQ(result.error->kind, argtool::ParseErrorKind::MutexConflict);
    EXPECT_EQ(result.error->message, "Use either --json or --plain.");
}

TEST(ArgtoolTests, DependencyConstraintWorks)
{
    argtool::Parser parser;
    parser.SetProgramName("app.exe");
    parser.Option("server", 's').String().Done().Option("port", 'p').Int().Done().Positional("input").String().Done().AddDependency({"server", "port", "--server requires --port."});

    const std::vector<std::string> args = {"app.exe", "--server", "127.0.0.1", "input.txt"};
    const auto argv = ToArgv(args);

    const auto result = parser.Parse(static_cast<int>(argv.size()), argv.data());
    ASSERT_FALSE(result.ok);
    ASSERT_TRUE(result.error.has_value());
    EXPECT_EQ(result.error->kind, argtool::ParseErrorKind::DependencyError);
}

TEST(ArgtoolTests, ConstraintPipelineUsesPriority)
{
    argtool::Parser parser;
    parser.SetProgramName("app.exe")
        .Positional("input")
        .String()
        .Done();

    parser.AddConstraintRule(argtool::ConstraintRule{
        "low_rule",
        argtool::RulePriority::Low,
        argtool::RuleGroup::Custom,
        true,
        [](const argtool::ConstraintContext &)
        {
            argtool::ConstraintResult out;
            out.ok = false;
            out.error.kind = argtool::ParseErrorKind::InvalidValue;
            out.error.field = "low";
            out.error.token = "low";
            out.error.message = "low rule fired";
            return out;
        }});

    parser.AddConstraintRule(argtool::ConstraintRule{
        "high_rule",
        argtool::RulePriority::High,
        argtool::RuleGroup::Custom,
        true,
        [](const argtool::ConstraintContext &)
        {
            argtool::ConstraintResult out;
            out.ok = false;
            out.error.kind = argtool::ParseErrorKind::InvalidValue;
            out.error.field = "high";
            out.error.token = "high";
            out.error.message = "high rule fired";
            return out;
        }});

    const std::vector<std::string> args = {"app.exe", "input.txt"};
    const auto argv = ToArgv(args);

    const auto result = parser.Parse(static_cast<int>(argv.size()), argv.data());
    ASSERT_FALSE(result.ok);
    ASSERT_TRUE(result.error.has_value());
    EXPECT_EQ(result.error->field, "high");
    EXPECT_EQ(result.error->message, "high rule fired");
}

TEST(ArgtoolTests, ConstraintPipelineDeferredFailureWorks)
{
    CaptureLogger logger;
    argtool::Parser parser;
    parser.SetProgramName("app.exe")
        .SetLogger(&logger)
        .Positional("input")
        .String()
        .Done();

    parser.AddConstraintRule(argtool::ConstraintRule{
        "deferred_rule",
        argtool::RulePriority::Normal,
        argtool::RuleGroup::Custom,
        false,
        [](const argtool::ConstraintContext &)
        {
            argtool::ConstraintResult out;
            out.ok = false;
            out.error.kind = argtool::ParseErrorKind::InvalidValue;
            out.error.field = "deferred";
            out.error.token = "deferred";
            out.error.message = "deferred rule fired";
            return out;
        }});

    const std::vector<std::string> args = {"app.exe", "input.txt"};
    const auto argv = ToArgv(args);
    const auto result = parser.Parse(static_cast<int>(argv.size()), argv.data());
    ASSERT_FALSE(result.ok);
    ASSERT_TRUE(result.error.has_value());
    EXPECT_EQ(result.error->field, "deferred");
    EXPECT_EQ(result.error->message, "deferred rule fired");
    ASSERT_EQ(logger.warnings.size(), 1U);
    EXPECT_NE(logger.warnings.front().find("deferred"), std::string::npos);
}

TEST(ArgtoolTests, BuiltinConvertersSupportUnits)
{
    argtool::Parser parser;
    parser.SetProgramName("app.exe")
        .Option("timeout", 't')
        .Int()
        .Done()
        .Option("window", 'w')
        .Double()
        .Done()
        .Positional("input")
        .String()
        .Done();

    const std::vector<std::string> args = {"app.exe", "--timeout", "2s", "--window", "1.5kb", "input.txt"};
    const auto argv = ToArgv(args);
    const auto result = parser.Parse(static_cast<int>(argv.size()), argv.data());

    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.GetInt("timeout"), 2000);
    EXPECT_DOUBLE_EQ(result.GetDouble("window"), 1536.0);
}

TEST(ArgtoolTests, LocalConverterOverridesGlobalConverter)
{
    argtool::Parser parser;
    parser.SetProgramName("app.exe")
        .SetGlobalConverter(argtool::ValueType::String,
                            [](std::string_view raw)
                            {
                                argtool::ConvertResult out;
                                out.ok = true;
                                out.value = std::string(raw);
                                for (char &ch : out.value)
                                {
                                    ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
                                }
                                return out;
                            });

    parser.Option("mode", 'm')
        .String()
        .ConvertWith([](std::string_view raw)
                     {
                         argtool::ConvertResult out;
                         out.ok = true;
                         out.value = "local:" + std::string(raw);
                         return out; })
        .Done()
        .Positional("input")
        .String()
        .Done();

    const std::vector<std::string> args = {"app.exe", "--mode", "mix", "abc.txt"};
    const auto argv = ToArgv(args);
    const auto result = parser.Parse(static_cast<int>(argv.size()), argv.data());

    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.GetString("mode"), "local:mix");
    EXPECT_EQ(result.GetString("input"), "ABC.TXT");
}

TEST(ArgtoolTests, OptionalAndListCardinalityWork)
{
    argtool::Parser parser;
    parser.SetProgramName("app.exe")
        .Option("tag", 'T')
        .String()
        .ListValue()
        .Done()
        .Positional("input")
        .String()
        .OptionalValue()
        .Done();

    const std::vector<std::string> args = {"app.exe", "--tag", "x", "--tag", "y"};
    const auto argv = ToArgv(args);
    const auto result = parser.Parse(static_cast<int>(argv.size()), argv.data());

    ASSERT_TRUE(result.ok);
    const auto tags = result.GetAll("tag");
    ASSERT_EQ(tags.size(), 2U);
    EXPECT_EQ(tags[0], "x");
    EXPECT_EQ(tags[1], "y");
    EXPECT_FALSE(result.Has("input"));
}

TEST(ArgtoolTests, ListCardinalityRequiresAppend)
{
    argtool::Parser parser;
    EXPECT_THROW(
        parser.Option("tag", 't')
            .String()
            .ListValue()
            .Repeat(argtool::RepeatMode::Override)
            .Done(),
        std::invalid_argument);
}

TEST(ArgtoolTests, UnknownOptionHandlerCanSwallowUnknown)
{
    argtool::Parser parser;
    parser.SetProgramName("app.exe")
        .SetUnknownOptionHandler([](std::string_view token, std::string *error_message)
                                 {
              (void)error_message;
              return token == "--legacy"; });

    parser.Option("output", 'o').String().Done().Positional("input").String().Done();

    const std::vector<std::string> args = {"app.exe", "--legacy", "--output", "app.log", "input.txt"};
    const auto argv = ToArgv(args);

    const auto result = parser.Parse(static_cast<int>(argv.size()), argv.data());
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.GetString("output"), "app.log");
}

TEST(ArgtoolTests, LongAliasParsesAsCanonicalOption)
{
    argtool::Parser parser;
    parser.SetProgramName("app.exe");
    parser.Option("output", 'o')
        .Alias("out")
        .String()
        .Done()
        .Positional("input")
        .String()
        .Done();

    const std::vector<std::string> args = {"app.exe", "--out", "alias.log", "input.txt"};
    const auto argv = ToArgv(args);
    const auto result = parser.Parse(static_cast<int>(argv.size()), argv.data());
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.GetString("output"), "alias.log");
}

TEST(ArgtoolTests, ShortAliasParsesAsCanonicalOption)
{
    argtool::Parser parser;
    parser.SetProgramName("app.exe");
    parser.Option("output", 'o')
        .ShortAlias('O')
        .String()
        .Done()
        .Positional("input")
        .String()
        .Done();

    const std::vector<std::string> args = {"app.exe", "-O", "alias.log", "input.txt"};
    const auto argv = ToArgv(args);
    const auto result = parser.Parse(static_cast<int>(argv.size()), argv.data());
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.GetString("output"), "alias.log");
}

TEST(ArgtoolTests, BoolFlagDefaultModeIsSwitch)
{
    argtool::Parser parser;
    parser.SetProgramName("app.exe")
        .Flag("verbose", 'v')
        .Description("Verbose")
        .Done()
        .Positional("input")
        .String()
        .Done();

    const std::vector<std::string> args = {"app.exe", "-vvv", "input.txt"};
    const auto argv = ToArgv(args);
    const auto result = parser.Parse(static_cast<int>(argv.size()), argv.data());
    ASSERT_TRUE(result.ok);
    EXPECT_TRUE(result.GetBool("verbose"));
    EXPECT_EQ(result.GetCount("verbose"), 1);
}

TEST(ArgtoolTests, BoolFlagToggleModeWorks)
{
    argtool::Parser parser;
    parser.SetProgramName("app.exe")
        .Flag("feature", 'f')
        .BoolMode(argtool::BoolFlagMode::Toggle)
        .Done()
        .Positional("input")
        .String()
        .Done();

    const std::vector<std::string> args = {"app.exe", "-fff", "input.txt"};
    const auto argv = ToArgv(args);
    const auto result = parser.Parse(static_cast<int>(argv.size()), argv.data());
    ASSERT_TRUE(result.ok);
    EXPECT_TRUE(result.GetBool("feature"));
}

TEST(ArgtoolTests, DoneValidatesIntRangeBoundaries)
{
    argtool::Parser parser;
    EXPECT_THROW(
        parser.Option("big", 'b')
            .Int()
            .Range(0.0, 3000000000.0)
            .Done(),
        std::invalid_argument);
}

TEST(ArgtoolTests, DoneValidatesDefaultAgainstRange)
{
    argtool::Parser parser;
    EXPECT_THROW(
        parser.Option("level", 'l')
            .Int()
            .Range(0.0, 5.0)
            .Default("7")
            .Done(),
        std::invalid_argument);
}

TEST(ArgtoolTests, TemplateApiBuildsOptionAndPositional)
{
    argtool::Parser parser;
    parser.SetProgramName("app.exe");
    argtool::OptionTemplate option_tpl;
    option_tpl.long_name = "mode";
    option_tpl.short_name = 'm';
    option_tpl.value_type = argtool::ValueType::String;
    option_tpl.required = false;
    option_tpl.repeat_mode = argtool::RepeatMode::Override;
    option_tpl.bool_mode = argtool::BoolFlagMode::Switch;
    option_tpl.default_value = std::string("Debug");
    option_tpl.value_name = "MODE";
    option_tpl.description = "Build mode";
    option_tpl.choices = {"Debug", "Release"};
    option_tpl.range_policy = argtool::RangePolicy::Fail;
    parser.AddOptionTemplate(option_tpl);

    argtool::PositionalTemplate positional_tpl;
    positional_tpl.name = "input";
    positional_tpl.value_type = argtool::ValueType::String;
    positional_tpl.required = true;
    positional_tpl.variadic = false;
    positional_tpl.description = "Input file";
    positional_tpl.range_policy = argtool::RangePolicy::Fail;
    parser.AddPositionalTemplate(positional_tpl);

    const std::vector<std::string> args = {"app.exe", "input.txt"};
    const auto argv = ToArgv(args);
    const auto result = parser.Parse(static_cast<int>(argv.size()), argv.data());
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.GetString("mode"), "Debug");
    EXPECT_EQ(result.GetString("input"), "input.txt");
}

TEST(ArgtoolTests, LoggerReceivesErrors)
{
    CaptureLogger logger;
    auto parser = BuildParser(&logger);
    const std::vector<std::string> args = {"app.exe", "--unknown", "input.txt"};
    const auto argv = ToArgv(args);

    const auto result = parser.Parse(static_cast<int>(argv.size()), argv.data());
    ASSERT_FALSE(result.ok);
    ASSERT_EQ(logger.errors.size(), 1U);
    EXPECT_EQ(logger.errors[0].kind, argtool::ParseErrorKind::UnknownOption);
}

TEST(ArgtoolTests, HelpContainsTableHeadersAndConstraintsColumn)
{
    auto parser = BuildParser();
    parser.AddMutexGroup({{"output", "level"}, "Use either --output or --level."});
    parser.AddDependency({"level", "output", "--level requires --output."});
    parser.MutableSubcommands().Register("build", [](const std::vector<std::string> &)
                                         { return 0; }, "Build the project");
    parser.MutableSubcommands().Register("clean", [](const std::vector<std::string> &)
                                         { return 0; }, "Clean build artifacts");

    const std::string help = parser.HelpText();
    EXPECT_NE(help.find("Flags:"), std::string::npos);
    EXPECT_NE(help.find("Value Options:"), std::string::npos);
    EXPECT_NE(help.find("Positional Arguments:"), std::string::npos);
    EXPECT_NE(help.find("Relations:"), std::string::npos);
    EXPECT_NE(help.find("Mutex Groups:"), std::string::npos);
    EXPECT_NE(help.find("Dependencies:"), std::string::npos);
    EXPECT_NE(help.find("Type"), std::string::npos);
    EXPECT_NE(help.find("Required"), std::string::npos);
    EXPECT_NE(help.find("Default"), std::string::npos);
    EXPECT_NE(help.find("Repeat"), std::string::npos);
    EXPECT_NE(help.find("override"), std::string::npos);
    EXPECT_NE(help.find("Description [Constraints]"), std::string::npos);
    EXPECT_NE(help.find("Subcommands:"), std::string::npos);
    EXPECT_NE(help.find("build"), std::string::npos);
    EXPECT_NE(help.find("clean"), std::string::npos);
}

TEST(ArgtoolTests, DeclarativeApiRejectsDuplicateOptionNames)
{
    argtool::Parser parser;
    parser.Option("output", 'o').String().Done();

    EXPECT_THROW(parser.Option("output", 'p').String().Done(), std::invalid_argument);
    EXPECT_THROW(parser.Option("other", 'o').String().Done(), std::invalid_argument);
}

TEST(ArgtoolTests, DeclarativeApiRejectsPositionalConflicts)
{
    argtool::Parser parser;
    parser.Option("input", 'i').String().Done();
    EXPECT_THROW(parser.Positional("input").String().Done(), std::invalid_argument);
}

TEST(ArgtoolTests, TwoLevelSubcommandPathIsCaptured)
{
    argtool::Parser parser;
    parser.SetProgramName("app.exe")
        .AddSubcommandRoot("repo", "Repository commands")
        .AddSubcommandLeaf("repo", "sync", "Sync repository")
        .Option("force", 'f')
        .BoolFlag()
        .Done();

    const std::vector<std::string> args = {"app.exe", "repo", "sync", "--force"};
    const auto argv = ToArgv(args);
    const auto result = parser.Parse(static_cast<int>(argv.size()), argv.data());

    ASSERT_TRUE(result.ok);
    ASSERT_TRUE(result.subcommand_path.has_value());
    EXPECT_EQ(result.subcommand_path->root, "repo");
    EXPECT_EQ(result.subcommand_path->leaf, "sync");
    EXPECT_EQ(result.GetString("subcommand.path"), "repo.sync");
}

TEST(ArgtoolTests, JsonOutputContainsTraceAndSubcommand)
{
    argtool::Parser parser;
    parser.SetProgramName("app.exe")
        .EnableTrace(true)
        .AddSubcommandRoot("repo", "Repository commands")
        .AddSubcommandLeaf("repo", "sync", "Sync repository")
        .Option("timeout", 't')
        .Int()
        .Done();

    const std::vector<std::string> args = {"app.exe", "repo", "sync", "--timeout", "2s"};
    const auto argv = ToArgv(args);
    const auto result = parser.Parse(static_cast<int>(argv.size()), argv.data());
    ASSERT_TRUE(result.ok);

    const std::string json = parser.ResultToJson(result, true);
    EXPECT_NE(json.find("\"ok\":true"), std::string::npos);
    EXPECT_NE(json.find("\"subcommand\""), std::string::npos);
    EXPECT_NE(json.find("\"root\":\"repo\""), std::string::npos);
    EXPECT_NE(json.find("\"leaf\":\"sync\""), std::string::npos);
    EXPECT_NE(json.find("\"trace\""), std::string::npos);
    EXPECT_NE(json.find("\"timeout\""), std::string::npos);
}

TEST(ArgtoolTests, LegacyProfileSupportsQuestionMarkHelp)
{
    argtool::Parser parser;
    parser.SetProgramName("app.exe")
        .EnableLegacyProfile(true)
        .SetHelpLayout(argtool::HelpLayout::Compact)
        .AddSubcommandRoot("repo", "Repository commands")
        .AddSubcommandLeaf("repo", "sync", "Sync repository")
        .Option("output", 'o')
        .String()
        .Done();

    const std::vector<std::string> args = {"app.exe", "-?"};
    const auto argv = ToArgv(args);
    const auto result = parser.Parse(static_cast<int>(argv.size()), argv.data());
    ASSERT_TRUE(result.ok);
    EXPECT_TRUE(result.help_requested);

    const std::string help = parser.HelpText();
    EXPECT_NE(help.find("Options (compact):"), std::string::npos);
    EXPECT_NE(help.find("Subcommand Tree (compact):"), std::string::npos);
    EXPECT_NE(help.find("Legacy profile: enabled"), std::string::npos);
}

TEST(ArgtoolTests, JsonContractContainsSchemaAndStableValueOrder)
{
    argtool::Parser parser;
    parser.SetProgramName("app.exe")
        .Option("zeta", 'z')
        .String()
        .Done()
        .Option("alpha", 'a')
        .String()
        .Done();

    const std::vector<std::string> args = {"app.exe", "--zeta", "last", "--alpha", "first"};
    const auto argv = ToArgv(args);
    const auto result = parser.Parse(static_cast<int>(argv.size()), argv.data());
    ASSERT_TRUE(result.ok);

    const std::string json = parser.ResultToJson(result, false);
    EXPECT_NE(json.find("\"schema\":\"argtool.parse.result\""), std::string::npos);
    EXPECT_NE(json.find("\"schema_version\":1"), std::string::npos);

    const std::size_t alpha_pos = json.find("\"alpha\"");
    const std::size_t zeta_pos = json.find("\"zeta\"");
    ASSERT_NE(alpha_pos, std::string::npos);
    ASSERT_NE(zeta_pos, std::string::npos);
    EXPECT_LT(alpha_pos, zeta_pos);
}

TEST(ArgtoolTests, HelpTextLayoutOverloadSwitchesOutput)
{
    argtool::Parser parser;
    parser.SetProgramName("app.exe")
        .Option("output", 'o')
        .String()
        .Done()
        .Positional("input")
        .String()
        .Done();

    const std::string fixed = parser.HelpText(argtool::HelpLayout::Fixed);
    const std::string compact = parser.HelpText(argtool::HelpLayout::Compact);

    EXPECT_NE(fixed.find("Flags:"), std::string::npos);
    EXPECT_NE(fixed.find("Value Options:"), std::string::npos);
    EXPECT_EQ(fixed.find("Options (compact):"), std::string::npos);

    EXPECT_NE(compact.find("Options (compact):"), std::string::npos);
    EXPECT_NE(compact.find("Positionals (compact):"), std::string::npos);
    EXPECT_EQ(compact.find("Value Options:"), std::string::npos);
}

TEST(ArgtoolTests, LegacyProfileOffDoesNotTreatQuestionMarkAsHelp)
{
    argtool::Parser parser;
    parser.SetProgramName("app.exe").EnableLegacyProfile(false);

    const std::vector<std::string> args = {"app.exe", "-?"};
    const auto argv = ToArgv(args);
    const auto result = parser.Parse(static_cast<int>(argv.size()), argv.data());

    ASSERT_FALSE(result.ok);
    ASSERT_TRUE(result.error.has_value());
    EXPECT_EQ(result.error->kind, argtool::ParseErrorKind::UnknownOption);
}

TEST(ArgtoolTests, JsonOutputCanExcludeTrace)
{
    argtool::Parser parser;
    parser.SetProgramName("app.exe")
        .EnableTrace(true)
        .Option("output", 'o')
        .String()
        .Done();

    const std::vector<std::string> args = {"app.exe", "--output", "a.log"};
    const auto argv = ToArgv(args);
    const auto result = parser.Parse(static_cast<int>(argv.size()), argv.data());
    ASSERT_TRUE(result.ok);

    const std::string with_trace = parser.ResultToJson(result, true);
    const std::string without_trace = parser.ResultToJson(result, false);
    EXPECT_NE(with_trace.find("\"trace\""), std::string::npos);
    EXPECT_EQ(without_trace.find("\"trace\""), std::string::npos);
}

TEST(ArgtoolTests, DefaultHelpLayoutFollowsSetter)
{
    argtool::Parser parser;
    parser.SetProgramName("app.exe")
        .SetHelpLayout(argtool::HelpLayout::Compact)
        .Option("output", 'o')
        .String()
        .Done();

    const std::string help = parser.HelpText();
    EXPECT_NE(help.find("Options (compact):"), std::string::npos);
    EXPECT_EQ(help.find("Value Options:"), std::string::npos);
}
