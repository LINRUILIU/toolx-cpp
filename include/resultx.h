#pragma once

#include <string>
#include <string_view>
#include <utility>

#include "asyncx.h"
#include "cfgx.h"
#include "fsx.h"
#include "httpx.h"
#include "sysx.h"

namespace resultx
{

    using ErrorDomain = sysx::ErrorDomain;
    using ErrorKind = sysx::ErrorKind;
    using Error = sysx::Error;
    using Status = sysx::Status;

    template <typename T>
    using Result = sysx::Result<T>;

    inline Error MakeError(ErrorKind kind,
                           std::string message,
                           ErrorDomain domain = ErrorDomain::System,
                           int native_code = 0,
                           bool retryable = false)
    {
        Error error;
        error.kind = kind;
        error.domain = domain;
        error.native_code = native_code;
        error.retryable = retryable;
        error.message = std::move(message);
        return error;
    }

    inline Status OkStatus()
    {
        return sysx::OkStatus();
    }

    inline ErrorKind MapAsyncxErrorKind(asyncx::ErrorKind kind) noexcept
    {
        switch (kind)
        {
        case asyncx::ErrorKind::None:
            return ErrorKind::None;
        case asyncx::ErrorKind::InvalidArgument:
            return ErrorKind::InvalidArgument;
        case asyncx::ErrorKind::Timeout:
            return ErrorKind::TimedOut;
        case asyncx::ErrorKind::QueueFull:
        case asyncx::ErrorKind::QueueClosed:
        case asyncx::ErrorKind::NotRunning:
            return ErrorKind::WouldBlock;
        case asyncx::ErrorKind::NotFound:
            return ErrorKind::NotFound;
        case asyncx::ErrorKind::Internal:
        default:
            return ErrorKind::Internal;
        }
    }

    inline ErrorKind MapHttpxErrorKind(httpx::ErrorKind kind) noexcept
    {
        switch (kind)
        {
        case httpx::ErrorKind::None:
            return ErrorKind::None;
        case httpx::ErrorKind::InvalidArgument:
            return ErrorKind::InvalidArgument;
        case httpx::ErrorKind::InvalidUrl:
        case httpx::ErrorKind::UnsupportedScheme:
            return ErrorKind::NotSupported;
        case httpx::ErrorKind::Timeout:
            return ErrorKind::TimedOut;
        case httpx::ErrorKind::Network:
            return ErrorKind::NetworkUnreachable;
        case httpx::ErrorKind::Proxy:
        case httpx::ErrorKind::Tls:
        case httpx::ErrorKind::Protocol:
        case httpx::ErrorKind::Redirect:
        case httpx::ErrorKind::TransportUnavailable:
        case httpx::ErrorKind::Internal:
        default:
            return ErrorKind::Internal;
        }
    }

    inline Status FromSysx(const sysx::Status &status)
    {
        return status;
    }

    template <typename T>
    inline Result<T> FromSysx(const sysx::Result<T> &result)
    {
        return result;
    }

    inline Status FromCfgx(const cfgx::Status &status,
                           ErrorKind kind = ErrorKind::InvalidArgument,
                           ErrorDomain domain = ErrorDomain::System)
    {
        if (status.ok)
        {
            return OkStatus();
        }

        return Status{false, MakeError(kind, status.error, domain)};
    }

    template <typename T>
    inline Result<T> FromCfgx(const cfgx::Result<T> &result,
                              ErrorKind kind = ErrorKind::InvalidArgument,
                              ErrorDomain domain = ErrorDomain::System)
    {
        if (result.ok)
        {
            return Result<T>{true, result.value, {}};
        }

        return Result<T>{false, {}, MakeError(kind, result.error, domain)};
    }

    inline Status FromFsx(const fsx::Status &status,
                          ErrorKind kind = ErrorKind::Internal,
                          ErrorDomain domain = ErrorDomain::System)
    {
        if (status.ok)
        {
            return OkStatus();
        }

        return Status{false, MakeError(kind, status.error, domain)};
    }

    inline Result<fsx::RunResult> FromFsx(const fsx::RunResult &result,
                                          ErrorKind kind = ErrorKind::Internal,
                                          ErrorDomain domain = ErrorDomain::System)
    {
        if (result.ok)
        {
            return Result<fsx::RunResult>{true, result, {}};
        }

        return Result<fsx::RunResult>{false, {}, MakeError(kind, result.error, domain)};
    }

    inline Status FromAsyncx(const asyncx::Status &status)
    {
        if (status.ok)
        {
            return OkStatus();
        }

        return Status{false,
                      MakeError(MapAsyncxErrorKind(status.error.kind),
                                status.error.message,
                                ErrorDomain::System,
                                0,
                                status.error.retryable)};
    }

    template <typename T>
    inline Result<T> FromAsyncx(const asyncx::Result<T> &result)
    {
        if (result.ok)
        {
            return Result<T>{true, result.value, {}};
        }

        return Result<T>{false,
                         {},
                         MakeError(MapAsyncxErrorKind(result.error.kind),
                                   result.error.message,
                                   ErrorDomain::System,
                                   0,
                                   result.error.retryable)};
    }

    inline Status FromHttpx(const httpx::Status &status)
    {
        if (status.ok)
        {
            return OkStatus();
        }

        return Status{false,
                      MakeError(MapHttpxErrorKind(status.error.kind),
                                status.error.message,
                                ErrorDomain::Network,
                                status.error.http_status,
                                status.error.retryable)};
    }

    template <typename T>
    inline Result<T> FromHttpx(const httpx::Result<T> &result)
    {
        if (result.ok)
        {
            return Result<T>{true, result.value, {}};
        }

        return Result<T>{false,
                         {},
                         MakeError(MapHttpxErrorKind(result.error.kind),
                                   result.error.message,
                                   ErrorDomain::Network,
                                   result.error.http_status,
                                   result.error.retryable)};
    }

    inline std::string FormatError(const Error &error)
    {
        std::string out = std::string(sysx::ToString(error.domain)) + ":"
                        + sysx::ToString(error.kind);
        if (!error.message.empty())
        {
            out += " ";
            out += error.message;
        }
        if (error.native_code != 0)
        {
            out += " (native=" + std::to_string(error.native_code) + ")";
        }
        return out;
    }

    template <typename T>
    inline Result<T> Propagate(Status status)
    {
        if (status.ok)
        {
            return Result<T>{true, T{}, {}};
        }
        return Result<T>{false, {}, status.error};
    }

} // namespace resultx
