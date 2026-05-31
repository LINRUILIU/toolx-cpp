#include "argtool.h"

#include <iostream>
#include <string_view>
#include <vector>

namespace
{

class ParseLogger final : public argtool::IParseLogger
{
  public:
    void OnError(const argtool::ParseError& error) override
    {
        std::cout << "parse-error: " << error.message << "\n";
    }

    void OnWarning(std::string_view message) override
    {
        std::cout << "parse-warning: " << message << "\n";
    }
};

void TypedOptions()
{
    // Scenario 1: typed options, defaults, ranges, and choices.
    argtool::Parser parser;
    parser.SetProgramName("copy")
        .Option("threads", 'j')
        .Int()
        .Default("4")
        .Range(1, 64)
        .Description("Worker count.")
        .Done()
        .Option("mode")
        .String()
        .Default("safe")
        .Choices({"safe", "fast"})
        .Done()
        .Positional("input")
        .String()
        .Description("Input file.")
        .Done();

    const char* argv[] = {"copy", "--threads", "8", "--mode", "safe", "in.txt"};
    const auto result = parser.Parse(6, argv);
    std::cout << "typed ok=" << result.ok << " threads=" << result.GetInt("threads")
              << " input=" << result.GetString("input") << "\n";
}

void ListsAndCounters()
{
    // Scenario 2: repeatable list options and counted flags.
    argtool::Parser parser;
    parser.SetProgramName("pack")
        .Option("include")
        .String()
        .ListValue()
        .Description("Can be repeated.")
        .Done()
        .Flag("verbose", 'v')
        .BoolMode(argtool::BoolFlagMode::Count)
        .Done();

    const char* argv[] = {"pack", "--include", "src", "--include", "include", "-v", "-v"};
    const auto result = parser.Parse(7, argv);
    std::cout << "includes=" << result.GetAll("include").size() << " verbose=" << result.GetCount("verbose") << "\n";
}

void RelationsAndCustomRules()
{
    // Scenario 3: relationship rules keep CLI intent readable at the parser boundary.
    argtool::Parser parser;
    parser.SetProgramName("deploy")
        .Flag("dry-run")
        .Done()
        .Flag("force")
        .Done()
        .Option("token")
        .String()
        .Done()
        .Option("target")
        .String()
        .Required()
        .Done();
    parser.AddMutexGroup({{"dry-run", "force"}, "dry-run and force cannot be combined"});
    parser.AddDependency({"force", "token", "--force requires --token"});
    parser.AddConstraintRule({"target-prefix", argtool::RulePriority::Normal, argtool::RuleGroup::Custom, true,
                              [](const argtool::ConstraintContext& ctx)
                              {
                                  argtool::ConstraintResult out;
                                  if (ctx.result.GetString("target").rfind("prod-", 0) != 0)
                                  {
                                      out.ok = false;
                                      out.error.kind = argtool::ParseErrorKind::InvalidValue;
                                      out.error.field = "target";
                                      out.error.message = "target must start with prod-";
                                  }
                                  return out;
                              }});

    const char* argv[] = {"deploy", "--force", "--token", "secret", "--target", "prod-west"};
    const auto result = parser.Parse(6, argv);
    std::cout << "relations ok=" << result.ok << "\n";
}

void LoggingAndJson()
{
    // Scenario 4: parser trace, logger callbacks, and machine-readable diagnostics.
    ParseLogger logger;
    argtool::Parser parser;
    parser.SetProgramName("inspect").SetLogger(&logger).EnableTrace(true).Flag("json").Done();

    const char* argv[] = {"inspect", "--json"};
    const auto result = parser.Parse(2, argv);
    std::cout << "trace=" << result.trace.size() << " json-bytes=" << parser.ResultToJson(result).size() << "\n";
}

} // namespace

int main()
{
    TypedOptions();
    ListsAndCounters();
    RelationsAndCustomRules();
    LoggingAndJson();
    return 0;
}
