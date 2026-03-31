#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

#if defined(_WIN32)
#define SYSX_OS_WINDOWS 1
#else
#define SYSX_OS_WINDOWS 0
#endif

#if defined(__linux__)
#define SYSX_OS_LINUX 1
#else
#define SYSX_OS_LINUX 0
#endif

#if defined(__APPLE__)
#define SYSX_OS_MACOS 1
#else
#define SYSX_OS_MACOS 0
#endif

#if defined(_MSC_VER)
#define SYSX_COMPILER_MSVC 1
#else
#define SYSX_COMPILER_MSVC 0
#endif

#if defined(__clang__)
#define SYSX_COMPILER_CLANG 1
#else
#define SYSX_COMPILER_CLANG 0
#endif

#if defined(__GNUC__) && !defined(__clang__)
#define SYSX_COMPILER_GCC 1
#else
#define SYSX_COMPILER_GCC 0
#endif

namespace sysx
{

    enum class OsKind
    {
        Unknown = 0,
        Windows,
        Linux,
        MacOS,
    };

    enum class CompilerKind
    {
        Unknown = 0,
        Msvc,
        Clang,
        Gcc,
    };

    enum class ErrorDomain
    {
        System = 0,
        Network,
    };

    enum class ErrorKind
    {
        None = 0,
        InvalidArgument,
        PermissionDenied,
        NotFound,
        AlreadyExists,
        Interrupted,
        TimedOut,
        WouldBlock,
        AddressInUse,
        ConnectionRefused,
        ConnectionReset,
        NetworkUnreachable,
        HostUnreachable,
        NotSupported,
        Internal,
        Unknown,
    };

    struct Error
    {
        ErrorKind kind{ErrorKind::None};
        ErrorDomain domain{ErrorDomain::System};
        int native_code{0};
        bool retryable{false};
        std::string message;
    };

    struct Status
    {
        bool ok{false};
        Error error{};
    };

    template <typename T>
    struct Result
    {
        bool ok{false};
        T value{};
        Error error{};
    };

    constexpr OsKind CurrentOs() noexcept
    {
#if SYSX_OS_WINDOWS
        return OsKind::Windows;
#elif SYSX_OS_LINUX
        return OsKind::Linux;
#elif SYSX_OS_MACOS
        return OsKind::MacOS;
#else
        return OsKind::Unknown;
#endif
    }

    constexpr CompilerKind CurrentCompiler() noexcept
    {
#if SYSX_COMPILER_MSVC
        return CompilerKind::Msvc;
#elif SYSX_COMPILER_CLANG
        return CompilerKind::Clang;
#elif SYSX_COMPILER_GCC
        return CompilerKind::Gcc;
#else
        return CompilerKind::Unknown;
#endif
    }

    constexpr bool IsWindows() noexcept
    {
        return CurrentOs() == OsKind::Windows;
    }

    constexpr bool IsLinux() noexcept
    {
        return CurrentOs() == OsKind::Linux;
    }

    constexpr bool IsMacOS() noexcept
    {
        return CurrentOs() == OsKind::MacOS;
    }

    const char *ToString(OsKind os) noexcept;
    const char *ToString(CompilerKind compiler) noexcept;
    const char *ToString(ErrorDomain domain) noexcept;
    const char *ToString(ErrorKind kind) noexcept;

    Error MakeError(ErrorDomain domain, int native_code, std::string message = {});
    Status OkStatus();
    Status MakeErrorStatus(ErrorDomain domain, int native_code, std::string message = {});
    Status MakeErrorStatus(Error error);
    Error LastSystemError(std::string message = {});
    Error LastNetworkError(std::string message = {});
    bool IsWouldBlockCode(ErrorDomain domain, int native_code) noexcept;

    namespace time
    {

        using SteadyClock = std::chrono::steady_clock;
        using SystemClock = std::chrono::system_clock;
        using SteadyTimePoint = SteadyClock::time_point;
        using SystemTimePoint = SystemClock::time_point;

        inline SteadyTimePoint SteadyNow() noexcept
        {
            return SteadyClock::now();
        }

        inline SystemTimePoint SystemNow() noexcept
        {
            return SystemClock::now();
        }

        inline std::int64_t SystemNowMs()
        {
            return static_cast<std::int64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(SystemNow().time_since_epoch()).count());
        }

        template <typename Rep, typename Period>
        inline void SleepFor(std::chrono::duration<Rep, Period> duration)
        {
            std::this_thread::sleep_for(duration);
        }

        template <typename Clock, typename Duration>
        inline void SleepUntil(std::chrono::time_point<Clock, Duration> deadline)
        {
            std::this_thread::sleep_until(deadline);
        }

        template <typename Rep, typename Period>
        inline SteadyTimePoint DeadlineAfter(std::chrono::duration<Rep, Period> timeout)
        {
            const auto casted = std::chrono::duration_cast<SteadyClock::duration>(timeout);
            return SteadyNow() + casted;
        }

    } // namespace time

    namespace sync
    {

        using Mutex = std::mutex;
        using RecursiveMutex = std::recursive_mutex;
        using ConditionVariable = std::condition_variable;
        using ConditionVariableAny = std::condition_variable_any;

    } // namespace sync

    namespace thread
    {

        class Thread
        {
        public:
            Thread() noexcept = default;

            template <typename Fn, typename... Args>
            explicit Thread(Fn &&fn, Args &&...args)
                : impl_(std::forward<Fn>(fn), std::forward<Args>(args)...)
            {
            }

            Thread(Thread &&other) noexcept = default;
            Thread &operator=(Thread &&other) noexcept = default;

            Thread(const Thread &) = delete;
            Thread &operator=(const Thread &) = delete;

            bool Joinable() const noexcept
            {
                return impl_.joinable();
            }

            void Join()
            {
                impl_.join();
            }

            void Detach()
            {
                impl_.detach();
            }

            std::thread::id GetId() const noexcept
            {
                return impl_.get_id();
            }

            std::thread::native_handle_type NativeHandle()
            {
                return impl_.native_handle();
            }

            void Swap(Thread &other) noexcept
            {
                impl_.swap(other.impl_);
            }

        private:
            std::thread impl_{};
        };

        inline std::size_t HardwareConcurrency() noexcept
        {
            return static_cast<std::size_t>(std::thread::hardware_concurrency());
        }

    } // namespace thread

} // namespace sysx
