#include <iostream>

#include "argtool.h"

class ConsoleParseLogger final : public argtool::IParseLogger
{
public:
    void OnError(const argtool::ParseError &error) override
    {
        std::cerr << "[parse-error] kind=" << static_cast<int>(error.kind)
                  << " field=" << error.field
                  << " token=" << error.token
                  << " message=" << error.message << "\n";
    }

    void OnWarning(std::string_view message) override
    {
        std::cerr << "[parse-warning] " << message << "\n";
    }
};

int main(int argc, const char *const argv[])
{
    ConsoleParseLogger logger;

    argtool::Parser parser;
    parser.SetDescription("argtool example - declarative CLI parsing")
        .SetUsageExample("app.exe -v --output build.log input.txt -- --literal-token")
        .SetHelpLayout(argtool::HelpLayout::Fixed)
        .EnableTrace(true)
        .SetLogger(&logger);

    // Iteration 5: keep a minimal compatibility bridge for legacy help behavior.
    parser.EnableLegacyProfile(false);

    parser.Flag("verbose", 'v')
        .Description("Enable verbose output.")
        .Done();

    parser.Option("output", 'o')
        .String()
        .ValueName("FILE")
        .Default("app.log")
        .Description("Output file path.")
        .Done();

    parser.Option("mode", 'm')
        .String()
        .Choices({"Debug", "Release"})
        .Description("Build mode.")
        .Done();

    parser.Option("tag", 't')
        .String()
        .ListValue()
        .Description("Repeatable tag list.")
        .Done();

    parser.Option("level", 'l')
        .Int()
        .Range(0, 5, argtool::RangePolicy::UseDefaultAndWarn)
        .Default("3")
        .Description("Log level in [0,5].")
        .Done();

    parser.Flag("json", 'j')
        .Description("Use JSON output.")
        .Done();

    parser.Flag("plain", 'p')
        .Description("Use plain output.")
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
        .Done()
        .AddMutexGroup({{"json", "plain"}, "Use either --json or --plain."})
        .AddDependency({"mode", "output", "--mode requires --output."})
        .AddConstraintRule({"custom.mode_release_requires_json",
                            argtool::RulePriority::Normal,
                            argtool::RuleGroup::Custom,
                            true,
                            [](const argtool::ConstraintContext &ctx)
                            {
                                argtool::ConstraintResult out;
                                const auto mode_it = ctx.result.values.find("mode");
                                const auto json_it = ctx.result.values.find("json");
                                const bool release_mode =
                                    (mode_it != ctx.result.values.end() && !mode_it->second.empty() && mode_it->second.back() == "Release");
                                const bool has_json = (json_it != ctx.result.values.end() && !json_it->second.empty());
                                if (release_mode && !has_json)
                                {
                                    out.ok = false;
                                    out.error.kind = argtool::ParseErrorKind::DependencyError;
                                    out.error.field = "mode";
                                    out.error.token = "json";
                                    out.error.message = "Release mode requires --json.";
                                }
                                return out;
                            }});

    parser.AddSubcommandRoot("repo", "Repository level commands")
        .AddSubcommandLeaf("repo", "sync", "Synchronize local and remote state")
        .AddSubcommandLeaf("repo", "status", "Show repository status");

    parser.MutableSubcommands().Register("build", [](const std::vector<std::string> &)
                                         { return 0; }, "Build the project");
    parser.MutableSubcommands().Register("clean", [](const std::vector<std::string> &)
                                         { return 0; }, "Clean build artifacts");

    const argtool::ParseResult result = parser.Parse(argc, argv);
    if (result.help_requested)
    {
        std::cout << parser.HelpText();
        return result.exit_code;
    }

    if (!result.ok)
    {
        if (result.error.has_value())
        {
            std::cerr << "error: " << result.error->message << "\n\n";
        }
        std::cerr << parser.HelpText();
        return result.exit_code;
    }

    std::cout << "result.json=" << parser.ResultToJson(result, true) << "\n";

    std::cout << "verbose_count=" << result.GetCount("verbose") << "\n";
    std::cout << "output=" << result.GetString("output") << "\n";
    std::cout << "mode=" << result.GetString("mode", "Debug") << "\n";
    std::cout << "level=" << result.GetInt("level") << "\n";
    std::cout << "input=" << result.GetString("input") << "\n";

    const auto extras = result.GetAll("extras");
    if (!extras.empty())
    {
        std::cout << "extras:";
        for (const auto &item : extras)
        {
            std::cout << " [" << item << "]";
        }
        std::cout << "\n";
    }

    const auto tags = result.GetAll("tag");
    if (!tags.empty())
    {
        std::cout << "tags:";
        for (const auto &item : tags)
        {
            std::cout << " [" << item << "]";
        }
        std::cout << "\n";
    }

    if (result.subcommand_path.has_value())
    {
        std::cout << "subcommand.root=" << result.subcommand_path->root << "\n";
        std::cout << "subcommand.leaf=" << result.subcommand_path->leaf << "\n";
    }

    return 0;
}
