#include <gtest/gtest.h>

#include <atomic>
#include <chrono>

#include "sysx.h"

#if defined(_WIN32)
#include <winsock2.h>
#include <windows.h>
#else
#include <cerrno>
#endif

TEST(SysxPlatformTests, DetectCurrentPlatformAndCompiler)
{
    const auto os = sysx::CurrentOs();
    EXPECT_NE(os, sysx::OsKind::Unknown);

    const auto compiler = sysx::CurrentCompiler();
    EXPECT_NE(compiler, sysx::CompilerKind::Unknown);

#if defined(_WIN32)
    EXPECT_TRUE(sysx::IsWindows());
#else
    EXPECT_FALSE(sysx::IsWindows());
#endif
}

TEST(SysxErrorTests, MakeErrorFromCode)
{
    const auto ok = sysx::MakeError(sysx::ErrorDomain::System, 0);
    EXPECT_EQ(ok.kind, sysx::ErrorKind::None);
    EXPECT_EQ(ok.native_code, 0);

#if defined(_WIN32)
    const auto timeout = sysx::MakeError(sysx::ErrorDomain::Network, WSAETIMEDOUT);
#else
    const auto timeout = sysx::MakeError(sysx::ErrorDomain::Network, ETIMEDOUT);
#endif
    EXPECT_EQ(timeout.kind, sysx::ErrorKind::TimedOut);
    EXPECT_TRUE(timeout.retryable);
}

TEST(SysxErrorTests, StatusHelpersMatchModuleStyle)
{
    const auto ok = sysx::OkStatus();
    EXPECT_TRUE(ok.ok);

#if defined(_WIN32)
    const auto bad = sysx::MakeErrorStatus(sysx::ErrorDomain::System, ERROR_ACCESS_DENIED, "denied");
#else
    const auto bad = sysx::MakeErrorStatus(sysx::ErrorDomain::System, EACCES, "denied");
#endif
    EXPECT_FALSE(bad.ok);
    EXPECT_EQ(bad.error.kind, sysx::ErrorKind::PermissionDenied);
    EXPECT_EQ(bad.error.message, "denied");
}

TEST(SysxErrorTests, WouldBlockCheck)
{
#if defined(_WIN32)
    EXPECT_TRUE(sysx::IsWouldBlockCode(sysx::ErrorDomain::Network, WSAEWOULDBLOCK));
#else
    EXPECT_TRUE(sysx::IsWouldBlockCode(sysx::ErrorDomain::Network, EWOULDBLOCK));
#endif
    EXPECT_FALSE(sysx::IsWouldBlockCode(sysx::ErrorDomain::System, 0));
}

TEST(SysxErrorTests, LastSystemErrorCapturesNativeCode)
{
#if defined(_WIN32)
    SetLastError(ERROR_ACCESS_DENIED);
#else
    errno = EACCES;
#endif

    const auto err = sysx::LastSystemError("system failure");
    EXPECT_EQ(err.domain, sysx::ErrorDomain::System);
    EXPECT_EQ(err.kind, sysx::ErrorKind::PermissionDenied);
    EXPECT_EQ(err.message, "system failure");
}

TEST(SysxErrorTests, LastNetworkErrorCapturesNativeCode)
{
#if defined(_WIN32)
    WSASetLastError(WSAECONNREFUSED);
#else
    errno = ECONNREFUSED;
#endif

    const auto err = sysx::LastNetworkError("network failure");
    EXPECT_EQ(err.domain, sysx::ErrorDomain::Network);
    EXPECT_EQ(err.kind, sysx::ErrorKind::ConnectionRefused);
    EXPECT_EQ(err.message, "network failure");
}

TEST(SysxTimeTests, SteadyClockMovesForward)
{
    const auto begin = sysx::time::SteadyNow();
    sysx::time::SleepFor(std::chrono::milliseconds(5));
    const auto end = sysx::time::SteadyNow();
    EXPECT_GE(end, begin);

    const auto now_ms = sysx::time::SystemNowMs();
    EXPECT_GT(now_ms, 0);
}

TEST(SysxTimeTests, DeadlineAfterProducesFutureTimePoint)
{
    const auto begin = sysx::time::SteadyNow();
    const auto deadline = sysx::time::DeadlineAfter(std::chrono::milliseconds(5));
    EXPECT_GT(deadline, begin);
}

TEST(SysxThreadTests, ThreadRunsAndJoins)
{
    std::atomic<int> value{0};
    sysx::thread::Thread worker([&value]()
                                { value.store(7, std::memory_order_relaxed); });

    ASSERT_TRUE(worker.Joinable());
    worker.Join();
    EXPECT_EQ(value.load(std::memory_order_relaxed), 7);
}

TEST(SysxSyncTests, ConditionVariableWaitForTimesOut)
{
    sysx::sync::Mutex mu;
    sysx::sync::ConditionVariable cv;
    bool ready = false;

    std::unique_lock<sysx::sync::Mutex> lock(mu);
    const bool ok = cv.wait_for(lock, std::chrono::milliseconds(10), [&ready]()
                                { return ready; });
    EXPECT_FALSE(ok);
}

TEST(SysxErrorTests, NetworkErrorClassificationCoversReachabilityCases)
{
#if defined(_WIN32)
    const auto net_unreachable = sysx::MakeError(sysx::ErrorDomain::Network, WSAENETUNREACH);
    const auto host_unreachable = sysx::MakeError(sysx::ErrorDomain::Network, WSAEHOSTUNREACH);
#else
    const auto net_unreachable = sysx::MakeError(sysx::ErrorDomain::Network, ENETUNREACH);
    const auto host_unreachable = sysx::MakeError(sysx::ErrorDomain::Network, EHOSTUNREACH);
#endif

    EXPECT_EQ(net_unreachable.kind, sysx::ErrorKind::NetworkUnreachable);
    EXPECT_TRUE(net_unreachable.retryable);
    EXPECT_EQ(host_unreachable.kind, sysx::ErrorKind::HostUnreachable);
    EXPECT_TRUE(host_unreachable.retryable);
}

TEST(SysxErrorTests, MakeErrorProvidesOkMessageForZeroCode)
{
    const auto ok = sysx::MakeError(sysx::ErrorDomain::System, 0);
    EXPECT_EQ(ok.kind, sysx::ErrorKind::None);
    EXPECT_EQ(ok.message, "ok");
    EXPECT_FALSE(ok.retryable);
}
