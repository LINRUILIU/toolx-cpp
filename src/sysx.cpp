#include "sysx.h"

#include <cerrno>
#include <cstring>
#include <system_error>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>
#endif

namespace sysx
{

    namespace
    {

        ErrorKind MapSystemCode(int code) noexcept
        {
#if defined(_WIN32)
            switch (code)
            {
            case ERROR_SUCCESS:
                return ErrorKind::None;
            case ERROR_INVALID_PARAMETER:
                return ErrorKind::InvalidArgument;
            case ERROR_ACCESS_DENIED:
                return ErrorKind::PermissionDenied;
            case ERROR_FILE_NOT_FOUND:
            case ERROR_PATH_NOT_FOUND:
                return ErrorKind::NotFound;
            case ERROR_ALREADY_EXISTS:
            case ERROR_FILE_EXISTS:
                return ErrorKind::AlreadyExists;
            case ERROR_TIMEOUT:
                return ErrorKind::TimedOut;
            case ERROR_NOT_SUPPORTED:
                return ErrorKind::NotSupported;
            default:
                return ErrorKind::Unknown;
            }
#else
            switch (code)
            {
            case 0:
                return ErrorKind::None;
            case EINVAL:
                return ErrorKind::InvalidArgument;
            case EACCES:
            case EPERM:
                return ErrorKind::PermissionDenied;
            case ENOENT:
                return ErrorKind::NotFound;
            case EEXIST:
                return ErrorKind::AlreadyExists;
            case EINTR:
                return ErrorKind::Interrupted;
            case ETIMEDOUT:
                return ErrorKind::TimedOut;
            case EAGAIN:
            case EWOULDBLOCK:
                return ErrorKind::WouldBlock;
            case ENOTSUP:
#if defined(EOPNOTSUPP) && EOPNOTSUPP != ENOTSUP
            case EOPNOTSUPP:
#endif
                return ErrorKind::NotSupported;
            default:
                return ErrorKind::Unknown;
            }
#endif
        }

        ErrorKind MapNetworkCode(int code) noexcept
        {
#if defined(_WIN32)
            switch (code)
            {
            case 0:
                return ErrorKind::None;
            case WSAEINTR:
                return ErrorKind::Interrupted;
            case WSAEINVAL:
                return ErrorKind::InvalidArgument;
            case WSAEWOULDBLOCK:
            case WSAEINPROGRESS:
            case WSAEALREADY:
                return ErrorKind::WouldBlock;
            case WSAEACCES:
                return ErrorKind::PermissionDenied;
            case WSAEADDRINUSE:
                return ErrorKind::AddressInUse;
            case WSAECONNREFUSED:
                return ErrorKind::ConnectionRefused;
            case WSAECONNRESET:
                return ErrorKind::ConnectionReset;
            case WSAETIMEDOUT:
                return ErrorKind::TimedOut;
            case WSAENETUNREACH:
                return ErrorKind::NetworkUnreachable;
            case WSAEHOSTUNREACH:
                return ErrorKind::HostUnreachable;
            default:
                return ErrorKind::Unknown;
            }
#else
            switch (code)
            {
            case 0:
                return ErrorKind::None;
            case EINTR:
                return ErrorKind::Interrupted;
            case EINVAL:
                return ErrorKind::InvalidArgument;
            case EAGAIN:
            case EWOULDBLOCK:
            case EINPROGRESS:
            case EALREADY:
                return ErrorKind::WouldBlock;
            case EACCES:
            case EPERM:
                return ErrorKind::PermissionDenied;
            case EADDRINUSE:
                return ErrorKind::AddressInUse;
            case ECONNREFUSED:
                return ErrorKind::ConnectionRefused;
            case ECONNRESET:
                return ErrorKind::ConnectionReset;
            case ETIMEDOUT:
                return ErrorKind::TimedOut;
            case ENETUNREACH:
                return ErrorKind::NetworkUnreachable;
            case EHOSTUNREACH:
                return ErrorKind::HostUnreachable;
            default:
                return ErrorKind::Unknown;
            }
#endif
        }

        bool IsRetryable(ErrorKind kind) noexcept
        {
            switch (kind)
            {
            case ErrorKind::Interrupted:
            case ErrorKind::TimedOut:
            case ErrorKind::WouldBlock:
            case ErrorKind::ConnectionReset:
            case ErrorKind::NetworkUnreachable:
            case ErrorKind::HostUnreachable:
                return true;
            default:
                return false;
            }
        }

        int ReadLastSystemCode() noexcept
        {
#if defined(_WIN32)
            return static_cast<int>(GetLastError());
#else
            return errno;
#endif
        }

        int ReadLastNetworkCode() noexcept
        {
#if defined(_WIN32)
            return static_cast<int>(WSAGetLastError());
#else
            return errno;
#endif
        }

    } // namespace

    const char *ToString(OsKind os) noexcept
    {
        switch (os)
        {
        case OsKind::Windows:
            return "windows";
        case OsKind::Linux:
            return "linux";
        case OsKind::MacOS:
            return "macos";
        default:
            return "unknown";
        }
    }

    const char *ToString(CompilerKind compiler) noexcept
    {
        switch (compiler)
        {
        case CompilerKind::Msvc:
            return "msvc";
        case CompilerKind::Clang:
            return "clang";
        case CompilerKind::Gcc:
            return "gcc";
        default:
            return "unknown";
        }
    }

    const char *ToString(ErrorDomain domain) noexcept
    {
        switch (domain)
        {
        case ErrorDomain::System:
            return "system";
        case ErrorDomain::Network:
            return "network";
        default:
            return "unknown";
        }
    }

    const char *ToString(ErrorKind kind) noexcept
    {
        switch (kind)
        {
        case ErrorKind::None:
            return "none";
        case ErrorKind::InvalidArgument:
            return "invalid_argument";
        case ErrorKind::PermissionDenied:
            return "permission_denied";
        case ErrorKind::NotFound:
            return "not_found";
        case ErrorKind::AlreadyExists:
            return "already_exists";
        case ErrorKind::Interrupted:
            return "interrupted";
        case ErrorKind::TimedOut:
            return "timed_out";
        case ErrorKind::WouldBlock:
            return "would_block";
        case ErrorKind::AddressInUse:
            return "address_in_use";
        case ErrorKind::ConnectionRefused:
            return "connection_refused";
        case ErrorKind::ConnectionReset:
            return "connection_reset";
        case ErrorKind::NetworkUnreachable:
            return "network_unreachable";
        case ErrorKind::HostUnreachable:
            return "host_unreachable";
        case ErrorKind::NotSupported:
            return "not_supported";
        case ErrorKind::Internal:
            return "internal";
        default:
            return "unknown";
        }
    }

    Error MakeError(ErrorDomain domain, int native_code, std::string message)
    {
        Error out;
        out.domain = domain;
        out.native_code = native_code;

        if (domain == ErrorDomain::Network)
        {
            out.kind = MapNetworkCode(native_code);
        }
        else
        {
            out.kind = MapSystemCode(native_code);
        }

        out.retryable = IsRetryable(out.kind);

        if (message.empty())
        {
            if (native_code == 0)
            {
                message = "ok";
            }
            else
            {
                message = std::error_code(native_code, std::system_category()).message();
            }
        }

        out.message = std::move(message);
        return out;
    }

    Status OkStatus()
    {
        Status out;
        out.ok = true;
        return out;
    }

    Status MakeErrorStatus(ErrorDomain domain, int native_code, std::string message)
    {
        Status out;
        out.ok = false;
        out.error = MakeError(domain, native_code, std::move(message));
        return out;
    }

    Status MakeErrorStatus(Error error)
    {
        Status out;
        out.ok = false;
        out.error = std::move(error);
        return out;
    }

    Error LastSystemError(std::string message)
    {
        return MakeError(ErrorDomain::System, ReadLastSystemCode(), std::move(message));
    }

    Error LastNetworkError(std::string message)
    {
        return MakeError(ErrorDomain::Network, ReadLastNetworkCode(), std::move(message));
    }

    bool IsWouldBlockCode(ErrorDomain domain, int native_code) noexcept
    {
#if defined(_WIN32)
        if (domain == ErrorDomain::Network)
        {
            return native_code == WSAEWOULDBLOCK || native_code == WSAEINPROGRESS || native_code == WSAEALREADY;
        }
        return false;
#else
        (void)domain;
        return native_code == EWOULDBLOCK || native_code == EAGAIN || native_code == EINPROGRESS || native_code == EALREADY;
#endif
    }

} // namespace sysx
