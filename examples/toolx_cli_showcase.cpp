#include <filesystem>
#include <iostream>
#include <string>

#include "argtool.h"
#include "asyncx.h"
#include "cfgx.h"
#include "fsx.h"
#include "logsys.h"
#include "textcodec.h"

int main(int argc, const char *const argv[])
{
    argtool::Parser parser;
    parser.SetDescription("ToolX end-to-end showcase")
        .Option("name", 'n')
        .String()
        .Default("toolx")
        .Done()
        .Option("port", 'p')
        .Int()
        .Default("8080")
        .Done()
        .Flag("verbose", 'v')
        .Done()
        .Positional("payload")
        .String()
        .Required(false)
        .Done();

    const auto parsed = parser.Parse(argc, argv);
    if (parsed.help_requested)
    {
        std::cout << parser.HelpText();
        return parsed.exit_code;
    }
    if (!parsed.ok)
    {
        std::cerr << (parsed.error.has_value() ? parsed.error->message : "parse failed") << '\n';
        return parsed.exit_code;
    }

    logsys::DefaultLoggerOptions log_options;
    log_options.level = logsys::LogLevel::Trace;
    log_options.record_level = logsys::LogLevel::Trace;
    log_options.enable_console = true;
    log_options.enable_file = false;
    log_options.enable_debugger = false;

    auto &logger = logsys::Logger::Instance();
    logger.ConfigureDefaultLogger(log_options);
    logger.SetDefaultOrigin(logsys::ErrorSource::Business,
                            logsys::ModuleId::BusinessCommon,
                            logsys::ErrorCategory::Business);

    cfgx::Node config = cfgx::Node::MakeObject();
    cfgx::SetNode(config, "service.name", cfgx::Node(parsed.GetString("name")));
    cfgx::SetNode(config, "service.port", cfgx::Node(static_cast<std::int64_t>(parsed.GetInt("port"))));
    cfgx::SetNode(config, "service.verbose", cfgx::Node(parsed.GetBool("verbose")));

    const std::string payload = parsed.GetString("payload", "hello toolx");
    asyncx::ThreadPool pool;
    auto encoded_task = pool.Submit([payload]()
                                    { return textcodec::base64_encode(payload); });
    if (!encoded_task.ok)
    {
        std::cerr << "async submit failed: " << encoded_task.error.message << '\n';
        return 2;
    }

    const std::string encoded = encoded_task.value.get();
    pool.StopAndJoin(asyncx::StopMode::Drain);
    cfgx::SetNode(config, "service.payload_base64", cfgx::Node(encoded));

    const auto root = std::filesystem::path("temp/toolx_cli_showcase");
    const auto config_path = (root / "app.json").string();
    const auto summary_path = (root / "summary.txt").string();
    const auto journal_path = (root / "summary.journal").string();

    const auto serialized = cfgx::ToJson(config, 2);
    fsx::BatchPlan plan;
    plan.AddAtomicWrite(config_path, serialized)
        .AddAtomicWrite(summary_path,
                        "name=" + parsed.GetString("name") + "\n"
                        + "port=" + std::to_string(parsed.GetInt("port")) + "\n"
                        + "payload_base64=" + encoded + "\n");

    fsx::RunOptions run_options;
    run_options.conflict_policy = fsx::ConflictPolicy::Overwrite;
    run_options.rollback_mode = fsx::RollbackMode::BestEffort;
    run_options.journal_path = journal_path;

    const auto run = fsx::Run(plan, run_options);
    if (!run.ok)
    {
        std::cerr << "fsx failed: " << run.error << '\n';
        return 2;
    }

    LOGI("toolx_cli_showcase wrote %s and %s",
         config_path.c_str(),
         summary_path.c_str());
    logger.Flush();

    std::cout << "config=" << config_path << '\n';
    std::cout << "summary=" << summary_path << '\n';
    std::cout << "payload_base64=" << encoded << '\n';
    return 0;
}
