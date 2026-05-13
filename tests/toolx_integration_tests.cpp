#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <future>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "argtool.h"
#include "asyncx.h"
#include "cfgx.h"
#include "fsx.h"
#include "httpx.h"
#include "logsys.h"

namespace
{

    std::filesystem::path IntegrationRoot()
    {
        return std::filesystem::current_path() / "toolx_test_tmp" / "toolx_integration_tests";
    }

    class MemorySink final : public logsys::ISink
    {
    public:
        void Write(const std::string &line) override
        {
            std::lock_guard<std::mutex> lock(mu_);
            lines.push_back(line);
        }

        void Flush() override {}

        std::vector<std::string> Snapshot() const
        {
            std::lock_guard<std::mutex> lock(mu_);
            return lines;
        }

    private:
        mutable std::mutex mu_;
        std::vector<std::string> lines;
    };

    struct RemoteFetcherScope
    {
        RemoteFetcherScope()
        {
            cfgx::SetRemoteFetcher({});
        }

        ~RemoteFetcherScope()
        {
            cfgx::SetRemoteFetcher({});
        }
    };

} // namespace

TEST(ToolxIntegrationTests, CfgxSaveTriggersFsxWatcherOnConfigChange)
{
    const auto root = IntegrationRoot() / "cfgx_fsx_watch";
    const auto file = root / "app.json";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);

    cfgx::Node initial = cfgx::Node::MakeObject();
    ASSERT_TRUE(cfgx::SetNode(initial, "svc.port", cfgx::Node(std::int64_t(8080))).ok);
    ASSERT_TRUE(cfgx::SaveToFile(initial, file.string()).ok);

    auto watcher = fsx::CreateFileWatcher(file.string());
    ASSERT_NE(watcher, nullptr);

    const auto baseline = watcher->Poll(0);
    ASSERT_TRUE(baseline.ok) << baseline.error;
    EXPECT_FALSE(baseline.has_event);

    cfgx::Node updated = cfgx::Node::MakeObject();
    ASSERT_TRUE(cfgx::SetNode(updated, "svc.port", cfgx::Node(std::int64_t(9090))).ok);

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    ASSERT_TRUE(cfgx::SaveToFile(updated, file.string()).ok);

    const auto changed = watcher->Poll(300);
    ASSERT_TRUE(changed.ok) << changed.error;
    ASSERT_TRUE(changed.has_event);
    EXPECT_EQ(changed.event.kind, fsx::WatchEventKind::Modified);
    EXPECT_EQ(changed.event.path, file.generic_string());

    std::filesystem::remove_all(root, ec);
}

TEST(ToolxIntegrationTests, AsyncxFsxBridgeCompletesBatchPlans)
{
    namespace fs = std::filesystem;

    const fs::path root = IntegrationRoot() / "asyncx_fsx";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);

    asyncx::PoolOptions options;
    options.worker_count = 3;
    options.queue_capacity = 16;
    asyncx::ThreadPool pool(options);

    std::vector<std::future<fsx::RunResult>> futures;
    for (int i = 0; i < 3; ++i)
    {
        fsx::BatchPlan plan;
        const auto from = (root / ("job" + std::to_string(i) + "_from.txt")).string();
        const auto to = (root / ("job" + std::to_string(i) + "_to.txt")).string();
        plan.AddAtomicWrite(from, "payload-" + std::to_string(i)).AddRename(from, to);

        fsx::RunOptions run_options;
        run_options.rollback_mode = fsx::RollbackMode::BestEffort;
        run_options.conflict_policy = fsx::ConflictPolicy::Overwrite;
        run_options.journal_path = (root / ("job" + std::to_string(i) + ".journal")).string();

        auto submitted = pool.Submit([plan, run_options]()
                                     { return fsx::Run(plan, run_options); });
        ASSERT_TRUE(submitted.ok) << submitted.error.message;
        futures.push_back(std::move(submitted.value));
    }

    const auto first = asyncx::WaitAnyFor(futures, std::chrono::milliseconds(800));
    ASSERT_TRUE(first.ok) << first.error.message;
    EXPECT_LT(first.value, futures.size());

    const auto all_status = asyncx::WaitAllFor(futures, std::chrono::seconds(2));
    ASSERT_TRUE(all_status.ok) << all_status.error.message;

    for (std::size_t i = 0; i < futures.size(); ++i)
    {
        const auto result = futures[i].get();
        ASSERT_TRUE(result.ok) << result.error;
        EXPECT_TRUE(fs::exists(root / ("job" + std::to_string(i) + "_to.txt")));
    }

    EXPECT_TRUE(pool.StopAndJoin(asyncx::StopMode::Drain).ok);
    fs::remove_all(root, ec);
}

TEST(ToolxIntegrationTests, AsyncxHttpxBridgeSchedulesMultipleRequests)
{
    httpx::ClientOptions client_options;
    client_options.transport = [](const httpx::Request &request, const httpx::ClientOptions &) -> httpx::Result<httpx::Response>
    {
        httpx::Result<httpx::Response> out;
        std::chrono::milliseconds delay{40};
        if (request.url.find("fast") != std::string::npos)
        {
            delay = std::chrono::milliseconds(10);
        }
        else if (request.url.find("slow") != std::string::npos)
        {
            delay = std::chrono::milliseconds(120);
        }
        std::this_thread::sleep_for(delay);
        out.ok = true;
        out.value.status_code = 200;
        out.value.body = request.url;
        return out;
    };

    httpx::Client client(client_options);

    asyncx::PoolOptions options;
    options.worker_count = 3;
    options.queue_capacity = 16;
    asyncx::ThreadPool pool(options);

    const std::vector<std::string> urls = {
        "https://demo.local/slow",
        "https://demo.local/fast",
        "https://demo.local/normal"};

    std::vector<std::future<httpx::Result<httpx::Response>>> futures;
    for (const auto &url : urls)
    {
        auto submitted = pool.Submit([&client, url]()
                                     {
                                         httpx::Request request;
                                         request.method = httpx::HttpMethod::Get;
                                         request.url = url;
                                         return client.Send(request);
                                     });
        ASSERT_TRUE(submitted.ok) << submitted.error.message;
        futures.push_back(std::move(submitted.value));
    }

    const auto first = asyncx::WaitAnyFor(futures, std::chrono::milliseconds(500));
    ASSERT_TRUE(first.ok) << first.error.message;
    EXPECT_EQ(first.value, 1u);

    const auto all_status = asyncx::WaitAllFor(futures, std::chrono::seconds(2));
    ASSERT_TRUE(all_status.ok) << all_status.error.message;

    for (auto &future : futures)
    {
        const auto result = future.get();
        ASSERT_TRUE(result.ok) << result.error.message;
        EXPECT_EQ(result.value.status_code, 200);
    }

    EXPECT_TRUE(pool.StopAndJoin(asyncx::StopMode::Drain).ok);
}

TEST(ToolxIntegrationTests, AsyncxLogsysBridgeFlushesTaskLogs)
{
    using namespace logsys;

    DefaultLoggerOptions log_options;
    log_options.level = LogLevel::Trace;
    log_options.record_level = LogLevel::Trace;
    log_options.enable_console = false;
    log_options.enable_file = false;
    log_options.enable_debugger = false;

    auto &logger = Logger::Instance();
    logger.ConfigureDefaultLogger(log_options);
    logger.SetDefaultOrigin(ErrorSource::Business, ModuleId::BusinessCommon, ErrorCategory::Business);

    auto sink = std::make_shared<MemorySink>();
    logger.AddDefaultSink(sink);

    asyncx::PoolOptions options;
    options.worker_count = 2;
    options.queue_capacity = 32;
    options.backpressure_policy = asyncx::BackpressurePolicy::Block;
    asyncx::ThreadPool pool(options);

    std::vector<std::future<int>> futures;
    for (int i = 0; i < 8; ++i)
    {
        auto submitted = pool.Submit([i]()
                                     {
                                         LOGI("asyncx-logsys task=%d", i);
                                         return i;
                                     });
        ASSERT_TRUE(submitted.ok) << submitted.error.message;
        futures.push_back(std::move(submitted.value));
    }

    const auto wait_status = asyncx::WaitAllFor(futures, std::chrono::seconds(2));
    ASSERT_TRUE(wait_status.ok) << wait_status.error.message;
    EXPECT_TRUE(pool.StopAndJoin(asyncx::StopMode::Drain).ok);
    logger.Flush();

    int sum = 0;
    for (auto &future : futures)
    {
        sum += future.get();
    }
    EXPECT_EQ(sum, 28);

    const auto lines = sink->Snapshot();
    ASSERT_GE(lines.size(), 8u);
    const auto found = std::find_if(lines.begin(), lines.end(), [](const std::string &line)
                                    { return line.find("asyncx-logsys task=7") != std::string::npos; });
    EXPECT_NE(found, lines.end());
}

TEST(ToolxIntegrationTests, CfgxHttpxRemoteFetcherLoadsRemoteConfig)
{
    RemoteFetcherScope scope;

    httpx::ClientOptions options;
    options.transport = [](const httpx::Request &request, const httpx::ClientOptions &) -> httpx::Result<httpx::Response>
    {
        httpx::Result<httpx::Response> out;
        out.ok = true;
        out.value.status_code = 200;
        out.value.body = std::string("{\"source\":\"") + request.url + "\",\"svc\":{\"port\":9443}}";
        return out;
    };
    httpx::Client client(options);

    cfgx::SetRemoteFetcher([&client](const cfgx::RemoteFetchRequest &request)
                           {
                               cfgx::Result<cfgx::RemoteFetchResponse> out;
                               const auto response = client.Get(request.url, request.headers);
                               if (!response.ok)
                               {
                                   out.ok = false;
                                   out.error = response.error.message;
                                   return out;
                               }

                               out.ok = true;
                               out.value.body = response.value.body;
                               out.value.status_code = response.value.status_code;
                               out.value.headers = response.value.headers;
                               return out;
                           });

    const auto loaded = cfgx::LoadFromRemote("https://config.test/runtime.json");
    ASSERT_TRUE(loaded.ok) << loaded.error;

    const auto source = cfgx::GetNode(loaded.value, "source");
    ASSERT_TRUE(source.ok) << source.error;
    EXPECT_EQ(source.value->AsString(), "https://config.test/runtime.json");

    const auto port = cfgx::GetNode(loaded.value, "svc.port");
    ASSERT_TRUE(port.ok) << port.error;
    EXPECT_EQ(port.value->AsInt(-1), 9443);
}

TEST(ToolxIntegrationTests, HttpxLoggerCanForwardIntoLogsys)
{
    using namespace logsys;

    DefaultLoggerOptions log_options;
    log_options.level = LogLevel::Trace;
    log_options.record_level = LogLevel::Trace;
    log_options.enable_console = false;
    log_options.enable_file = false;
    log_options.enable_debugger = false;

    auto &logger = Logger::Instance();
    logger.ConfigureDefaultLogger(log_options);

    auto sink = std::make_shared<MemorySink>();
    logger.AddDefaultSink(sink);

    httpx::ClientOptions options;
    options.transport = [](const httpx::Request &request, const httpx::ClientOptions &) -> httpx::Result<httpx::Response>
    {
        httpx::Result<httpx::Response> out;
        out.ok = true;
        out.value.status_code = 204;
        out.value.body = request.url;
        return out;
    };
    options.logger = [&logger](const httpx::LogEvent &event)
    {
        logger.LogDefaultf(LogLevel::Info,
                           __FILE__,
                           __LINE__,
                           __func__,
                           "httpx %s %s status=%d",
                           event.method.c_str(),
                           event.url.c_str(),
                           event.status_code);
    };

    httpx::Client client(options);
    const auto response = client.Get("https://service.local/ping");
    ASSERT_TRUE(response.ok) << response.error.message;
    logger.Flush();

    const auto lines = sink->Snapshot();
    const auto found = std::find_if(lines.begin(), lines.end(), [](const std::string &line)
                                    { return line.find("https://service.local/ping") != std::string::npos; });
    EXPECT_NE(found, lines.end());
}

TEST(ToolxIntegrationTests, CfgxCanAuthorLogsysConfigFile)
{
    using namespace logsys;

    const auto root = IntegrationRoot() / "cfgx_logsys";
    const auto file = root / "logger.json";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);

    cfgx::Node config = cfgx::Node::MakeObject();
    ASSERT_TRUE(cfgx::SetNode(config, "global_record_level", cfgx::Node("debug")).ok);
    ASSERT_TRUE(cfgx::SetNode(config, "global_output_level", cfgx::Node("error")).ok);
    ASSERT_TRUE(cfgx::SetNode(config, "global_enable_console", cfgx::Node(false)).ok);
    ASSERT_TRUE(cfgx::SetNode(config, "global_enable_file", cfgx::Node(false)).ok);
    ASSERT_TRUE(cfgx::SetNode(config, "global_enable_debugger", cfgx::Node(false)).ok);
    ASSERT_TRUE(cfgx::SetNode(config, "global_text_field_mask", cfgx::Node(std::int64_t(66))).ok);
    ASSERT_TRUE(cfgx::SaveToFile(config, file.string()).ok);

    auto &logger = Logger::Instance();
    ASSERT_TRUE(logger.LoadConfigV2FromJsonFile(file.string()));
    EXPECT_EQ(logger.RecordLevel(), LogLevel::Debug);
    EXPECT_EQ(logger.Level(), LogLevel::Error);
    EXPECT_EQ(logger.TextFieldMask(), 66u);

    std::filesystem::remove_all(root, ec);
}

TEST(ToolxIntegrationTests, ArgtoolOverridesCanBeAppliedToCfgxTree)
{
    argtool::Parser parser;
    parser.Option("port", 'p').Int().Default("8080").Done();
    parser.Flag("verbose", 'v').Done();

    const char *argv[] = {"demo", "--port", "9090", "--verbose"};
    const auto parsed = parser.Parse(4, argv);
    ASSERT_TRUE(parsed.ok);

    cfgx::Node root = cfgx::Node::MakeObject();
    ASSERT_TRUE(cfgx::SetNode(root, "service.port", cfgx::Node(std::int64_t(8080))).ok);
    ASSERT_TRUE(cfgx::SetNode(root, "service.verbose", cfgx::Node(false)).ok);

    ASSERT_TRUE(cfgx::SetNode(root, "service.port", cfgx::Node(static_cast<std::int64_t>(parsed.GetInt("port")))).ok);
    ASSERT_TRUE(cfgx::SetNode(root, "service.verbose", cfgx::Node(parsed.GetBool("verbose"))).ok);

    const auto port = cfgx::GetNode(root, "service.port");
    ASSERT_TRUE(port.ok) << port.error;
    EXPECT_EQ(port.value->AsInt(-1), 9090);

    const auto verbose = cfgx::GetNode(root, "service.verbose");
    ASSERT_TRUE(verbose.ok) << verbose.error;
    EXPECT_TRUE(verbose.value->AsBool(false));
}

TEST(ToolxIntegrationTests, ReleaseScenarioRemoteConfigSyncUsesCoreModules)
{
    namespace fs = std::filesystem;
    RemoteFetcherScope remote_scope;

    const fs::path root = IntegrationRoot() / "release_sync";
    const fs::path base_file = root / "base.json";
    const fs::path out_file = root / "resolved.json";
    const fs::path snapshot_file = root / "snapshot.json";
    const fs::path journal_file = root / "resolved.journal";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);

    cfgx::Node base = cfgx::Node::MakeObject();
    ASSERT_TRUE(cfgx::SetNode(base, "svc.host", cfgx::Node("127.0.0.1")).ok);
    ASSERT_TRUE(cfgx::SetNode(base, "svc.port", cfgx::Node(std::int64_t(8080))).ok);
    ASSERT_TRUE(cfgx::SaveToFile(base, base_file.string()).ok);

    using namespace logsys;
    DefaultLoggerOptions log_options;
    log_options.level = LogLevel::Info;
    log_options.record_level = LogLevel::Info;
    log_options.enable_console = false;
    log_options.enable_file = false;
    log_options.enable_debugger = false;

    auto &logger = Logger::Instance();
    logger.ConfigureDefaultLogger(log_options);
    auto sink = std::make_shared<MemorySink>();
    logger.AddDefaultSink(sink);

    httpx::ClientOptions client_options;
    client_options.transport = [](const httpx::Request &request, const httpx::ClientOptions &) -> httpx::Result<httpx::Response>
    {
        httpx::Result<httpx::Response> out;
        out.ok = true;
        out.value.status_code = 200;
        out.value.body = std::string("{\"remote\":\"") + request.url + "\",\"svc\":{\"port\":9443}}";
        return out;
    };
    httpx::Client client(client_options);

    cfgx::SetRemoteFetcher([&client](const cfgx::RemoteFetchRequest &request)
                           {
                               cfgx::Result<cfgx::RemoteFetchResponse> out;
                               const auto response = client.Get(request.url, request.headers);
                               if (!response.ok)
                               {
                                   out.ok = false;
                                   out.error = response.error.message;
                                   return out;
                               }

                               out.ok = true;
                               out.value.body = response.value.body;
                               out.value.status_code = response.value.status_code;
                               out.value.headers = response.value.headers;
                               return out;
                           });

    asyncx::PoolOptions pool_options;
    pool_options.worker_count = 2;
    pool_options.queue_capacity = 8;
    asyncx::ThreadPool pool(pool_options);

    auto base_task = pool.Submit([base_file]()
                                 { return cfgx::LoadFromFile(base_file.string()); });
    ASSERT_TRUE(base_task.ok) << base_task.error.message;

    auto remote_task = pool.Submit([]()
                                   { return cfgx::LoadFromRemote("https://config.test/release.json"); });
    ASSERT_TRUE(remote_task.ok) << remote_task.error.message;

    auto loaded_base = base_task.value.get();
    ASSERT_TRUE(loaded_base.ok) << loaded_base.error;
    auto loaded_remote = remote_task.value.get();
    ASSERT_TRUE(loaded_remote.ok) << loaded_remote.error;

    const auto composed = cfgx::ComposeLayers(loaded_base.value,
                                             std::nullopt,
                                             std::nullopt,
                                             nullptr,
                                             cfgx::ComposeOptions{},
                                             nullptr,
                                             loaded_remote.value);
    ASSERT_TRUE(composed.ok) << composed.error;

    const auto validation = cfgx::Validate(composed.value,
                                           {
                                               cfgx::RequirePathRule("svc.host"),
                                               cfgx::NumericRangeRule("svc.port", 1, 65535),
                                           });
    ASSERT_TRUE(validation.ok);

    fsx::BatchPlan plan;
    const std::string serialized = cfgx::ToJson(composed.value, 2);
    plan.AddAtomicWrite(out_file.string(), serialized)
        .AddAtomicWrite(snapshot_file.string(), serialized);

    fsx::RunOptions run_options;
    run_options.journal_path = journal_file.string();
    run_options.conflict_policy = fsx::ConflictPolicy::Overwrite;
    run_options.rollback_mode = fsx::RollbackMode::BestEffort;
    const auto run = fsx::Run(plan, run_options);
    ASSERT_TRUE(run.ok) << run.error;

    LOGI("release-scenario config sync wrote %s", out_file.string().c_str());
    logger.Flush();

    const auto loaded_out = cfgx::LoadFromFile(out_file.string());
    ASSERT_TRUE(loaded_out.ok) << loaded_out.error;
    const auto port = cfgx::GetNode(loaded_out.value, "svc.port");
    ASSERT_TRUE(port.ok) << port.error;
    EXPECT_EQ(port.value->AsInt(-1), 9443);
    EXPECT_TRUE(fs::exists(snapshot_file));

    const auto lines = sink->Snapshot();
    const auto found_log = std::find_if(lines.begin(), lines.end(), [](const std::string &line)
                                        { return line.find("release-scenario config sync wrote") != std::string::npos; });
    EXPECT_NE(found_log, lines.end());

    EXPECT_TRUE(pool.StopAndJoin(asyncx::StopMode::Drain).ok);
    fs::remove_all(root, ec);
}
