#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "argtool.h"
#include "asyncx.h"
#include "cfgx.h"
#include "fsx.h"
#include "hashx.h"
#include "httpx.h"
#include "logsys.h"
#include "resultx.h"
#include "schemax.h"
#include "sysx.h"
#include "textcodec.h"
#include "tuix.h"
#include "utils.h"

TEST(ApiSurfaceTests, PublicHeadersExposeCoreValueTypes)
{
    argtool::ParseResult parse_result;
    EXPECT_EQ(parse_result.exit_code, 2);
    EXPECT_STREQ(asyncx::ToString(asyncx::StopMode::Drain), "drain");
    EXPECT_STREQ(fsx::ToString(fsx::RollbackMode::BestEffort), "BestEffort");
    EXPECT_STREQ(httpx::ToString(httpx::HttpMethod::Get), "GET");
    EXPECT_STREQ(textcodec::ToString(textcodec::DecodeError::None), "None");

    cfgx::Node root = cfgx::Node::MakeObject();
    ASSERT_TRUE(cfgx::SetNode(root, "service.port", cfgx::Node(std::int64_t(8080))).ok);
    const auto port = cfgx::GetNode(root, "service.port");
    ASSERT_TRUE(port.ok);
    EXPECT_EQ(port.value->AsInt(-1), 8080);

    const auto hash = hashx::fnv1a32("toolx");
    EXPECT_EQ(hash, utils::hash::fnv1a32("toolx"));
}

TEST(ApiSurfaceTests, PublicResultAndStatusAdaptersCompileTogether)
{
    sysx::Status sys_status = sysx::OkStatus();
    const auto normalized_sys = resultx::FromSysx(sys_status);
    EXPECT_TRUE(normalized_sys.ok);

    cfgx::Status cfg_status;
    cfg_status.ok = false;
    cfg_status.error = "bad config";
    const auto normalized_cfg = resultx::FromCfgx(cfg_status, resultx::ErrorKind::InvalidArgument);
    EXPECT_FALSE(normalized_cfg.ok);
    EXPECT_EQ(normalized_cfg.error.kind, resultx::ErrorKind::InvalidArgument);

    httpx::Status http_status;
    http_status.error.kind = httpx::ErrorKind::Timeout;
    http_status.error.message = "timeout";
    const auto normalized_http = resultx::FromHttpx(http_status);
    EXPECT_FALSE(normalized_http.ok);
    EXPECT_EQ(normalized_http.error.kind, resultx::ErrorKind::TimedOut);
}

TEST(ApiSurfaceTests, PublicObjectsCanBeMinimallyInstantiated)
{
    argtool::Parser parser;
    parser.SetProgramName("toolx").Option("verbose", 'v').BoolFlag().Done();
    const char* argv[] = {"toolx", "--verbose"};
    const auto parsed = parser.Parse(2, argv);
    ASSERT_TRUE(parsed.ok);
    EXPECT_TRUE(parsed.GetBool("verbose"));

    asyncx::PoolOptions pool_options;
    pool_options.start_immediately = false;
    asyncx::ThreadPool pool(pool_options);
    EXPECT_FALSE(pool.IsRunning());

    httpx::ClientOptions client_options;
    client_options.transport = [](const httpx::Request&, const httpx::ClientOptions&)
    {
        httpx::Result<httpx::Response> out;
        out.ok = true;
        out.value.status_code = 204;
        return out;
    };
    httpx::Client client(client_options);
    const auto response = client.Get("http://example.test/surface");
    ASSERT_TRUE(response.ok);
    EXPECT_EQ(response.value.status_code, 204);

    logsys::LoggerConfigV2 log_config;
    log_config.global_enable_console = false;
    log_config.global_enable_file = false;
    log_config.global_enable_debugger = false;
    EXPECT_EQ(log_config.fatal_policy, logsys::FatalPolicy::FlushOnly);

    tuix::FrameBuffer frame(2, 1, ' ');
    EXPECT_TRUE(frame.Put(0, 0, "x"));
    const auto* cell = frame.Get(0, 0);
    ASSERT_NE(cell, nullptr);
    EXPECT_EQ(cell->utf8, "x");

    const auto schema = schemax::Compile(cfgx::Node::MakeObject());
    EXPECT_TRUE(schema.ok);
}
