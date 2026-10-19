#include <gtest/gtest.h>

#include <string>

#include "resultx.h"

TEST(ResultxTests, CfgxStatusCanBeNormalized)
{
    cfgx::Status source{false, "missing svc.port"};
    const auto status = resultx::FromCfgx(source, resultx::ErrorKind::NotFound);

    EXPECT_FALSE(status.ok);
    EXPECT_EQ(status.error.kind, resultx::ErrorKind::NotFound);
    EXPECT_EQ(status.error.domain, resultx::ErrorDomain::System);
    EXPECT_EQ(status.error.message, "missing svc.port");
}

TEST(ResultxTests, CfgxStatusHonorsExplicitKindAndDomain)
{
    cfgx::Status source{false, "remote config unavailable"};
    const auto status = resultx::FromCfgx(source, resultx::ErrorKind::NotSupported, resultx::ErrorDomain::Network);

    EXPECT_FALSE(status.ok);
    EXPECT_EQ(status.error.kind, resultx::ErrorKind::NotSupported);
    EXPECT_EQ(status.error.domain, resultx::ErrorDomain::Network);
    EXPECT_EQ(status.error.message, "remote config unavailable");
}

TEST(ResultxTests, AsyncxStatusMapsTimeoutIntoSysxModel)
{
    asyncx::Status source;
    source.ok = false;
    source.error.kind = asyncx::ErrorKind::Timeout;
    source.error.message = "queue wait timed out";
    source.error.retryable = true;

    const auto status = resultx::FromAsyncx(source);
    EXPECT_FALSE(status.ok);
    EXPECT_EQ(status.error.kind, resultx::ErrorKind::TimedOut);
    EXPECT_TRUE(status.error.retryable);
}

TEST(ResultxTests, HttpxResultMapsNetworkDomainAndStatusCode)
{
    httpx::Result<std::string> source;
    source.ok = false;
    source.error.kind = httpx::ErrorKind::Timeout;
    source.error.http_status = 504;
    source.error.message = "gateway timeout";

    const auto result = resultx::FromHttpx(source);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.error.kind, resultx::ErrorKind::TimedOut);
    EXPECT_EQ(result.error.domain, resultx::ErrorDomain::Network);
    EXPECT_EQ(result.error.native_code, 504);
}

TEST(ResultxTests, FormatErrorIncludesDomainKindAndMessage)
{
    const auto error =
        resultx::MakeError(resultx::ErrorKind::Internal, "bridge failed", resultx::ErrorDomain::System, 17);
    const auto text = resultx::FormatError(error);

    EXPECT_NE(text.find("system"), std::string::npos);
    EXPECT_NE(text.find("internal"), std::string::npos);
    EXPECT_NE(text.find("bridge failed"), std::string::npos);
    EXPECT_NE(text.find("17"), std::string::npos);
}

TEST(ResultxTests, FailedFsxRunPreservesPartialExecutionReport)
{
    fsx::RunResult source;
    source.error = "second write failed";
    source.completed_steps = 1;
    fsx::StepReport first;
    first.dst = "current.json";
    first.ok = true;
    first.rolled_back = true;
    source.steps.push_back(first);
    fsx::StepReport second;
    second.step = 1;
    second.dst = "blocked/snapshot.json";
    second.error = source.error;
    source.steps.push_back(second);

    const auto result = resultx::FromFsx(source, resultx::ErrorKind::PermissionDenied);
    ASSERT_FALSE(result.ok);
    EXPECT_EQ(result.error.message, source.error);
    EXPECT_EQ(result.value.completed_steps, 1U);
    ASSERT_EQ(result.value.steps.size(), 2U);
    EXPECT_EQ(result.value.steps[0].dst, "current.json");
    EXPECT_TRUE(result.value.steps[0].rolled_back);
    EXPECT_EQ(result.value.steps[1].error, source.error);
}
