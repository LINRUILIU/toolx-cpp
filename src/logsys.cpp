#include "logsys.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <regex>
#include <string_view>
#include <unordered_set>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace logsys
{

#if defined(_WIN32)
    namespace
    {
        std::mutex g_network_ref_mu;
        std::size_t g_network_ref_count = 0;

        bool AcquireNetworkRuntime()
        {
            std::lock_guard<std::mutex> lk(g_network_ref_mu);
            if (g_network_ref_count == 0)
            {
                WSADATA wsa_data{};
                if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0)
                {
                    return false;
                }
            }
            ++g_network_ref_count;
            return true;
        }

        void ReleaseNetworkRuntime()
        {
            std::lock_guard<std::mutex> lk(g_network_ref_mu);
            if (g_network_ref_count == 0)
            {
                return;
            }
            --g_network_ref_count;
            if (g_network_ref_count == 0)
            {
                WSACleanup();
            }
        }
    } // namespace
#else
    namespace
    {
        bool AcquireNetworkRuntime()
        {
            return true;
        }

        void ReleaseNetworkRuntime() {}
    } // namespace
#endif

    // 本文件实现日志系统核心逻辑及默认实现（格式化、sink、路由、计数等）。
    // 注：本文件的注释为中文，文件应使用 UTF-8 编码保存。
    namespace
    {
        constexpr ErrorCode kSystemIoOpenFailed = LOGSYS_MAKE_ERROR_CODE(ErrorSource::System, ModuleId::Sink, 1);
        constexpr ErrorCode kConfigParseFailed = LOGSYS_MAKE_ERROR_CODE(ErrorSource::Business, ModuleId::Config, 1);
        constexpr ErrorCode kThirdPartyCallFailed = LOGSYS_MAKE_ERROR_CODE(ErrorSource::ThirdParty, ModuleId::ThirdPartyAdapter, 1);

        // 格式化时间戳为可读字符串，精确到毫秒，供文本/JSON 输出使用。
        std::string FormatTime(const std::chrono::system_clock::time_point tp)
        {
            const auto tt = std::chrono::system_clock::to_time_t(tp);
            std::tm tmv{};
#if defined(_WIN32)
            localtime_s(&tmv, &tt);
#else
            localtime_r(&tt, &tmv);
#endif

            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()).count() % 1000;

            std::ostringstream os;
            os << std::put_time(&tmv, "%Y-%m-%d %H:%M:%S")
               << '.' << std::setw(3) << std::setfill('0') << ms;
            return os.str();
        }

        // 对 JSON 字符串进行最小逃逸：处理引号、反斜线、换行等。
        std::string EscapeJson(std::string s)
        {
            std::string out;
            out.reserve(s.size());
            for (char c : s)
            {
                switch (c)
                {
                case '"':
                    out += "\\\"";
                    break;
                case '\\':
                    out += "\\\\";
                    break;
                case '\n':
                    out += "\\n";
                    break;
                case '\r':
                    out += "\\r";
                    break;
                case '\t':
                    out += "\\t";
                    break;
                default:
                    out += c;
                    break;
                }
            }
            return out;
        }

        // 简单的文件名模式匹配：支持前缀 '*' 与子串匹配（用于 profile 选择）。
        bool MatchFilePattern(std::string_view pattern, std::string_view file)
        {
            if (pattern.empty())
            {
                return false;
            }
            if (pattern == "*")
            {
                return true;
            }
            if (pattern.front() == '*')
            {
                const std::string_view suffix = pattern.substr(1);
                return file.size() >= suffix.size() && file.substr(file.size() - suffix.size()) == suffix;
            }
            return file.find(pattern) != std::string_view::npos;
        }

        // 从 ErrorCode 中解析 ModuleId（根据位域定义）。
        ModuleId ModuleFromCode(ErrorCode code)
        {
            return static_cast<ModuleId>(ErrorModulePart(code));
        }

        void ApplyOptionalProfile(ResolvedProfileV2 &out, const ProfileConfigV2 &p)
        {
            if (p.text_field_mask.has_value())
            {
                out.text_field_mask = *p.text_field_mask;
            }
            if (p.output_level.has_value())
            {
                out.output_level = *p.output_level;
            }
            if (p.record_level.has_value())
            {
                out.record_level = *p.record_level;
            }
            if (p.enable_console.has_value())
            {
                out.enable_console = *p.enable_console;
            }
            if (p.enable_file.has_value())
            {
                out.enable_file = *p.enable_file;
            }
            if (p.enable_debugger.has_value())
            {
                out.enable_debugger = *p.enable_debugger;
            }
            if (p.output_order.has_value())
            {
                out.output_order = *p.output_order;
            }
        }
    } // namespace

    ResolvedProfileV2 ProfileResolverV2::Resolve(const LoggerConfigV2 &config,
                                                 std::string_view file_path,
                                                 ModuleId module)
    {
        // Resolve: 合并全局配置与匹配到的 module/file profile，返回最终生效的配置。
        // 优先级说明：module 匹配先应用，再应用 file_path 匹配（file 优先覆盖 module）。
        ResolvedProfileV2 result;
        result.record_level = config.global_record_level;
        result.output_level = config.global_output_level;
        result.text_field_mask = config.global_text_field_mask;
        result.enable_console = config.global_enable_console;
        result.enable_file = config.global_enable_file;
        result.enable_debugger = config.global_enable_debugger;
        result.output_order = config.output_order;

        // Priority rule: file path profile overrides module profile.
        for (const auto &profile : config.profiles)
        {
            if (profile.module.has_value() && profile.module.value() == module)
            {
                ApplyOptionalProfile(result, profile);
                break;
            }
        }

        for (const auto &profile : config.profiles)
        {
            if (profile.file_path_pattern.has_value() && MatchFilePattern(*profile.file_path_pattern, file_path))
            {
                ApplyOptionalProfile(result, profile);
                break;
            }
        }

        return result;
    }

    const char *ToString(LogLevel level) noexcept
    {
        switch (level)
        {
        case LogLevel::Trace:
            return "TRACE";
        case LogLevel::Debug:
            return "DEBUG";
        case LogLevel::Info:
            return "INFO";
        case LogLevel::Warning:
            return "WARNING";
        case LogLevel::Error:
            return "ERROR";
        case LogLevel::Fatal:
            return "FATAL";
        case LogLevel::Critical:
            return "CRITICAL";
        }
        return "UNKNOWN";
    }

    const char *ToString(ErrorCategory category) noexcept
    {
        switch (category)
        {
        case ErrorCategory::System:
            return "SYSTEM";
        case ErrorCategory::Io:
            return "IO";
        case ErrorCategory::Net:
            return "NET";
        case ErrorCategory::Db:
            return "DB";
        case ErrorCategory::Config:
            return "CONFIG";
        case ErrorCategory::Auth:
            return "AUTH";
        case ErrorCategory::Memory:
            return "MEMORY";
        case ErrorCategory::Business:
            return "BUSINESS";
        case ErrorCategory::ThirdParty:
            return "THIRD_PARTY";
        case ErrorCategory::Security:
            return "SECURITY";
        case ErrorCategory::Count:
            return "COUNT";
        }
        return "UNKNOWN";
    }

    const char *ToString(ActionHint hint) noexcept
    {
        switch (hint)
        {
        case ActionHint::Retry:
            return "retry";
        case ActionHint::Fallback:
            return "fallback";
        case ActionHint::Abort:
            return "abort";
        case ActionHint::Ignore:
            return "ignore";
        case ActionHint::Escalate:
            return "escalate";
        }
        return "unknown";
    }

    std::optional<LogLevel> ParseLogLevel(std::string_view level_text)
    {
        // 支持常见的大小写文本解析，同时允许别名（warn->warning）。
        std::string lowered(level_text);
        std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c)
                       { return static_cast<char>(std::tolower(c)); });

        if (lowered == "trace")
            return LogLevel::Trace;
        if (lowered == "debug")
            return LogLevel::Debug;
        if (lowered == "info")
            return LogLevel::Info;
        if (lowered == "warning" || lowered == "warn")
            return LogLevel::Warning;
        if (lowered == "error")
            return LogLevel::Error;
        if (lowered == "fatal")
            return LogLevel::Fatal;
        if (lowered == "critical")
            return LogLevel::Critical;

        return std::nullopt;
    }

    std::optional<OutputOrderMode> ParseOutputOrderMode(std::string_view mode_text)
    {
        // 将文本解析为 OutputOrderMode，支持数字或文本别名。
        std::string lowered(mode_text);
        lowered.erase(std::remove_if(lowered.begin(), lowered.end(), [](unsigned char c)
                                     { return std::isspace(c) != 0 || c == '"'; }),
                      lowered.end());
        std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c)
                       { return static_cast<char>(std::tolower(c)); });

        if (lowered == "by_time_mixed" || lowered == "time" || lowered == "mixed")
        {
            return OutputOrderMode::ByTimeMixed;
        }
        if (lowered == "by_level_grouped" || lowered == "level" || lowered == "grouped")
        {
            return OutputOrderMode::ByLevelGrouped;
        }
        return std::nullopt;
    }

    std::optional<ModuleId> ParseModuleId(std::string_view module_text)
    {
        // 解析模块标识，支持文本名或数字形式。
        std::string lowered(module_text);
        lowered.erase(std::remove_if(lowered.begin(), lowered.end(), [](unsigned char c)
                                     { return std::isspace(c) != 0 || c == '"'; }),
                      lowered.end());
        std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c)
                       { return static_cast<char>(std::tolower(c)); });

        if (lowered == "core")
            return ModuleId::Core;
        if (lowered == "sink")
            return ModuleId::Sink;
        if (lowered == "formatter")
            return ModuleId::Formatter;
        if (lowered == "encoding")
            return ModuleId::Encoding;
        if (lowered == "config")
            return ModuleId::Config;
        if (lowered == "security")
            return ModuleId::Security;
        if (lowered == "thirdpartyadapter" || lowered == "third_party_adapter")
            return ModuleId::ThirdPartyAdapter;
        if (lowered == "businesscommon" || lowered == "business_common")
            return ModuleId::BusinessCommon;

        return std::nullopt;
    }

    std::optional<LogLevel> ParseLogLevelFromToken(std::string_view token)
    {
        // 解析 token（可能是数字或文本）得到 LogLevel。
        std::string cleaned(token);
        cleaned.erase(std::remove_if(cleaned.begin(), cleaned.end(), [](unsigned char c)
                                     { return std::isspace(c) != 0 || c == '"'; }),
                      cleaned.end());

        if (cleaned.empty())
        {
            return std::nullopt;
        }

        if (std::all_of(cleaned.begin(), cleaned.end(), [](unsigned char c)
                        { return std::isdigit(c) != 0 || c == '-'; }))
        {
            const int v = std::atoi(cleaned.c_str());
            if (v >= static_cast<int>(LogLevel::Trace) && v <= static_cast<int>(LogLevel::Critical))
            {
                return static_cast<LogLevel>(v);
            }
            return std::nullopt;
        }

        return ParseLogLevel(cleaned);
    }

    std::optional<OutputOrderMode> ParseOutputOrderModeFromToken(std::string_view token)
    {
        // 解析 token（数字或文本）得到 OutputOrderMode。
        std::string cleaned(token);
        cleaned.erase(std::remove_if(cleaned.begin(), cleaned.end(), [](unsigned char c)
                                     { return std::isspace(c) != 0 || c == '"'; }),
                      cleaned.end());

        if (cleaned.empty())
        {
            return std::nullopt;
        }

        if (std::all_of(cleaned.begin(), cleaned.end(), [](unsigned char c)
                        { return std::isdigit(c) != 0 || c == '-'; }))
        {
            const int v = std::atoi(cleaned.c_str());
            if (v == static_cast<int>(OutputOrderMode::ByTimeMixed))
            {
                return OutputOrderMode::ByTimeMixed;
            }
            if (v == static_cast<int>(OutputOrderMode::ByLevelGrouped))
            {
                return OutputOrderMode::ByLevelGrouped;
            }
            return std::nullopt;
        }

        return ParseOutputOrderMode(cleaned);
    }

    std::optional<RollingTimeMode> ParseRollingTimeMode(std::string_view mode_text)
    {
        std::string lowered(mode_text);
        lowered.erase(std::remove_if(lowered.begin(), lowered.end(), [](unsigned char c)
                                     { return std::isspace(c) != 0 || c == '"'; }),
                      lowered.end());
        std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c)
                       { return static_cast<char>(std::tolower(c)); });

        if (lowered == "none" || lowered == "off" || lowered == "disabled")
        {
            return RollingTimeMode::None;
        }
        if (lowered == "day" || lowered == "daily")
        {
            return RollingTimeMode::Day;
        }
        if (lowered == "hour" || lowered == "hourly")
        {
            return RollingTimeMode::Hour;
        }
        return std::nullopt;
    }

    std::optional<RollingTimeMode> ParseRollingTimeModeFromToken(std::string_view token)
    {
        std::string cleaned(token);
        cleaned.erase(std::remove_if(cleaned.begin(), cleaned.end(), [](unsigned char c)
                                     { return std::isspace(c) != 0 || c == '"'; }),
                      cleaned.end());

        if (cleaned.empty())
        {
            return std::nullopt;
        }

        if (std::all_of(cleaned.begin(), cleaned.end(), [](unsigned char c)
                        { return std::isdigit(c) != 0 || c == '-'; }))
        {
            const int v = std::atoi(cleaned.c_str());
            if (v >= static_cast<int>(RollingTimeMode::None) && v <= static_cast<int>(RollingTimeMode::Hour))
            {
                return static_cast<RollingTimeMode>(v);
            }
            return std::nullopt;
        }

        return ParseRollingTimeMode(cleaned);
    }

    std::optional<std::string> ExtractJsonToken(std::string_view body, std::string_view key)
    {
        const std::string pattern = "\"" + std::string(key) + "\"\\s*:\\s*([^,}\\n\\r]+)";
        std::regex re(pattern, std::regex_constants::icase);
        std::smatch match;
        const std::string body_text(body);
        if (!std::regex_search(body_text, match, re) || match.size() < 2)
        {
            return std::nullopt;
        }
        return match[1].str();
    }

    std::optional<std::string> ExtractJsonObjectBody(std::string_view text, std::string_view key)
    {
        const std::string marker = "\"" + std::string(key) + "\"";
        const auto key_pos = text.find(marker);
        if (key_pos == std::string_view::npos)
        {
            return std::nullopt;
        }

        const auto brace_pos = text.find('{', key_pos + marker.size());
        if (brace_pos == std::string_view::npos)
        {
            return std::nullopt;
        }

        int depth = 0;
        for (std::size_t i = brace_pos; i < text.size(); ++i)
        {
            if (text[i] == '{')
            {
                ++depth;
            }
            else if (text[i] == '}')
            {
                --depth;
                if (depth == 0)
                {
                    return std::string(text.substr(brace_pos + 1, i - brace_pos - 1));
                }
            }
        }

        return std::nullopt;
    }

    std::optional<std::string> ExtractJsonArrayBody(std::string_view text, std::string_view key)
    {
        const std::string marker = "\"" + std::string(key) + "\"";
        const auto key_pos = text.find(marker);
        if (key_pos == std::string_view::npos)
        {
            return std::nullopt;
        }

        const auto bracket_pos = text.find('[', key_pos + marker.size());
        if (bracket_pos == std::string_view::npos)
        {
            return std::nullopt;
        }

        int depth = 0;
        for (std::size_t i = bracket_pos; i < text.size(); ++i)
        {
            if (text[i] == '[')
            {
                ++depth;
            }
            else if (text[i] == ']')
            {
                --depth;
                if (depth == 0)
                {
                    return std::string(text.substr(bracket_pos + 1, i - bracket_pos - 1));
                }
            }
        }

        return std::nullopt;
    }

    std::vector<std::string> SplitJsonObjectList(std::string_view array_body)
    {
        std::vector<std::string> out;
        std::size_t start = std::string_view::npos;
        int depth = 0;
        for (std::size_t i = 0; i < array_body.size(); ++i)
        {
            if (array_body[i] == '{')
            {
                if (depth == 0)
                {
                    start = i;
                }
                ++depth;
            }
            else if (array_body[i] == '}')
            {
                --depth;
                if (depth == 0 && start != std::string_view::npos)
                {
                    out.emplace_back(array_body.substr(start + 1, i - start - 1));
                    start = std::string_view::npos;
                }
            }
        }
        return out;
    }

    std::optional<bool> ParseBoolToken(const std::string &token)
    {
        std::string cleaned(token);
        cleaned.erase(std::remove_if(cleaned.begin(), cleaned.end(), [](unsigned char c)
                                     { return std::isspace(c) != 0 || c == '"'; }),
                      cleaned.end());
        std::transform(cleaned.begin(), cleaned.end(), cleaned.begin(), [](unsigned char c)
                       { return static_cast<char>(std::tolower(c)); });

        if (cleaned == "true")
        {
            return true;
        }
        if (cleaned == "false")
        {
            return false;
        }
        return std::nullopt;
    }

    std::string ColorizeForConsole(std::string_view line, LogLevel level, bool enabled)
    {
        // 根据级别添加 ANSI 颜色码，enabled=false 时返回原始字符串。
        if (!enabled)
        {
            return std::string(line);
        }

        const char *code = "\x1b[37m";
        switch (level)
        {
        case LogLevel::Trace:
            code = "\x1b[90m";
            break;
        case LogLevel::Debug:
            code = "\x1b[36m";
            break;
        case LogLevel::Info:
            code = "\x1b[32m";
            break;
        case LogLevel::Warning:
            code = "\x1b[33m";
            break;
        case LogLevel::Error:
            code = "\x1b[31m";
            break;
        case LogLevel::Fatal:
            code = "\x1b[1;31m";
            break;
        case LogLevel::Critical:
            code = "\x1b[1;35m";
            break;
        }

        std::string out;
        out.reserve(line.size() + 16);
        out += code;
        out += line;
        out += "\x1b[0m";
        return out;
    }

    bool LogEvent::SetField(std::string key, std::string value)
    {
        if (ext_count >= kMaxExtFields)
        {
            return false;
        }
        if (key.size() > kMaxKeyLen || value.size() > kMaxValueLen)
        {
            return false;
        }
        ext_fields[ext_count++] = ExtField{std::move(key), std::move(value)};
        return true;
    }

    const ErrorDictionary &ErrorDictionary::Instance()
    {
        static ErrorDictionary dict;
        return dict;
    }

    // ErrorDictionary: 内置的错误码到元信息映射，包含示例条目及唯一性校验。

    ErrorDictionary::ErrorDictionary()
    {
        entries_ = {
            {kSystemIoOpenFailed, ErrorSource::System, ModuleId::Sink, ErrorCategory::Io,
             "file open failed: %s", ActionHint::Fallback, false, 1, "", ""},
            {kConfigParseFailed, ErrorSource::Business, ModuleId::Config, ErrorCategory::Config,
             "config parse failed at key: %s", ActionHint::Abort, false, 1, "", ""},
            {kThirdPartyCallFailed, ErrorSource::ThirdParty, ModuleId::ThirdPartyAdapter, ErrorCategory::ThirdParty,
             "third party call failed: %s", ActionHint::Escalate, false, 1, "curl", "CURLE_*"},
        };

        std::unordered_set<ErrorCode> unique_codes;
        for (const auto &e : entries_)
        {
            (void)unique_codes.insert(e.code);
        }
    }

    std::optional<ErrorDictionaryEntry> ErrorDictionary::Find(ErrorCode code) const
    {
        for (const auto &entry : entries_)
        {
            if (entry.code == code)
            {
                return entry;
            }
        }
        return std::nullopt;
    }

    const std::vector<ErrorDictionaryEntry> &ErrorDictionary::Entries() const noexcept
    {
        return entries_;
    }

    std::string TextFormatter::Format(const LogEvent &event) const
    {
        // Keep the field sequence stable for grep/regex based triage.
        std::ostringstream os;
        const auto has = [&](TextField field)
        {
            return (event.text_field_mask & static_cast<std::uint32_t>(field)) != 0;
        };

        bool first = true;
        auto append_part = [&](const std::string &part)
        {
            if (part.empty())
            {
                return;
            }
            if (!first)
            {
                os << ' ';
            }
            os << part;
            first = false;
        };
        if (has(TextField::Timestamp))
        {
            append_part(FormatTime(event.timestamp));
        }
        if (has(TextField::Level))
        {
            append_part(ToString(event.level));
        }
        if (has(TextField::Code))
        {
            append_part(std::to_string(event.code));
        }
        if (has(TextField::Category))
        {
            append_part(ToString(event.category));
        }
        if (has(TextField::Location))
        {
            append_part(event.file + ":" + std::to_string(event.line));
        }
        if (has(TextField::Function))
        {
            append_part(event.function);
        }
        if (has(TextField::Message))
        {
            append_part(event.message);
        }

        if (has(TextField::SysCode) && event.sys_code != 0)
        {
            append_part("sysCode=" + std::to_string(event.sys_code));
        }
        if (has(TextField::Vendor) && !event.vendor_name.empty())
        {
            append_part("vendor_name=" + event.vendor_name);
        }
        if (has(TextField::Vendor) && !event.vendor_code.empty())
        {
            append_part("vendor_code=" + event.vendor_code);
        }

        if (has(TextField::ExtFields))
        {
            for (std::size_t i = 0; i < event.ext_count; ++i)
            {
                append_part(event.ext_fields[i].key + "=" + event.ext_fields[i].value);
            }
        }
        if (has(TextField::ThreadId) && event.thread_id != 0)
        {
            append_part("tid=" + std::to_string(event.thread_id));
        }

        return os.str();
    }

    std::string JsonFormatter::Format(const LogEvent &event) const
    {
        std::ostringstream os;
        os << '{'
           << "\"time\":\"" << EscapeJson(FormatTime(event.timestamp)) << "\","
           << "\"level\":\"" << ToString(event.level) << "\","
           << "\"code\":" << event.code << ','
           << "\"category\":\"" << ToString(event.category) << "\","
           << "\"file\":\"" << EscapeJson(event.file) << "\","
           << "\"line\":" << event.line << ','
           << "\"func\":\"" << EscapeJson(event.function) << "\","
           << "\"msg\":\"" << EscapeJson(event.message) << "\"";

        if (event.sys_code != 0)
        {
            os << ",\"sysCode\":" << event.sys_code;
        }
        if (!event.vendor_name.empty())
        {
            os << ",\"vendor_name\":\"" << EscapeJson(event.vendor_name) << "\"";
        }
        if (!event.vendor_code.empty())
        {
            os << ",\"vendor_code\":\"" << EscapeJson(event.vendor_code) << "\"";
        }

        if (event.ext_count > 0)
        {
            os << ",\"ext\":{";
            for (std::size_t i = 0; i < event.ext_count; ++i)
            {
                if (i > 0)
                {
                    os << ',';
                }
                os << "\"" << EscapeJson(event.ext_fields[i].key) << "\":\""
                   << EscapeJson(event.ext_fields[i].value) << "\"";
            }
            os << '}';
        }

        os << '}';
        return os.str();
    }

    void ConsoleSink::Write(const std::string &line)
    {
        std::lock_guard<std::mutex> lk(mu_);
        std::clog << line << '\n';
    }

    void ConsoleSink::Flush()
    {
        std::lock_guard<std::mutex> lk(mu_);
        std::clog.flush();
    }

    FileSink::FileSink(std::string path, RollingConfigV2 rolling)
        : path_(std::move(path)), rolling_(rolling)
    {
        OpenLocked();
    }

    FileSink::~FileSink()
    {
        if (file_ != nullptr)
        {
            std::fclose(file_);
            file_ = nullptr;
        }
    }

    void FileSink::OpenLocked()
    {
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
        file_ = std::fopen(path_.c_str(), "ab");
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

        current_size_ = 0;
        if (std::filesystem::exists(path_))
        {
            std::error_code ec;
            const auto sz = std::filesystem::file_size(path_, ec);
            if (!ec)
            {
                current_size_ = static_cast<std::size_t>(sz);
            }
        }

        current_time_slot_key_ = (rolling_.time_mode == RollingTimeMode::None) ? std::string{} : CurrentTimeSlotKey();
    }

    std::string FileSink::RotatedPath(std::size_t index) const
    {
        return path_ + "." + std::to_string(index);
    }

    std::string FileSink::CurrentTimeSlotKey() const
    {
        if (rolling_.time_mode == RollingTimeMode::None)
        {
            return {};
        }

        const auto now = std::chrono::system_clock::now();
        const auto tt = std::chrono::system_clock::to_time_t(now);
        std::tm tmv{};
#if defined(_WIN32)
        localtime_s(&tmv, &tt);
#else
        localtime_r(&tt, &tmv);
#endif

        std::ostringstream os;
        if (rolling_.time_mode == RollingTimeMode::Day)
        {
            os << std::put_time(&tmv, "%Y%m%d");
        }
        else
        {
            os << std::put_time(&tmv, "%Y%m%d%H");
        }
        return os.str();
    }

    std::string FileSink::TimeRotatedPath(std::string_view slot_key) const
    {
        return path_ + "." + std::string(slot_key);
    }

    void FileSink::PruneTimeRotatedFilesLocked()
    {
        if (rolling_.time_mode == RollingTimeMode::None)
        {
            return;
        }
        if (rolling_.keep_recent_files == 0)
        {
            return;
        }

        const std::filesystem::path base_path(path_);
        const auto parent = base_path.parent_path().empty() ? std::filesystem::path(".") : base_path.parent_path();
        const auto stem = base_path.filename().string() + ".";
        const std::size_t expected_len = (rolling_.time_mode == RollingTimeMode::Day) ? 8u : 10u;

        std::vector<std::pair<std::string, std::filesystem::path>> entries;
        std::error_code ec_iter;
        for (const auto &entry : std::filesystem::directory_iterator(parent, ec_iter))
        {
            if (ec_iter || !entry.is_regular_file())
            {
                continue;
            }

            const auto name = entry.path().filename().string();
            if (name.size() <= stem.size() || name.rfind(stem, 0) != 0)
            {
                continue;
            }

            const auto suffix = name.substr(stem.size());
            if (suffix.size() != expected_len)
            {
                continue;
            }
            if (!std::all_of(suffix.begin(), suffix.end(), [](unsigned char c)
                             { return std::isdigit(c) != 0; }))
            {
                continue;
            }

            entries.push_back({suffix, entry.path()});
        }

        std::sort(entries.begin(), entries.end(), [](const auto &lhs, const auto &rhs)
                  { return lhs.first < rhs.first; });

        while (entries.size() > rolling_.keep_recent_files)
        {
            std::error_code ec_remove;
            std::filesystem::remove(entries.front().second, ec_remove);
            entries.erase(entries.begin());
        }
    }

    void FileSink::RotateByTimeLocked()
    {
        if (rolling_.time_mode == RollingTimeMode::None || current_time_slot_key_.empty())
        {
            return;
        }

        if (file_ != nullptr)
        {
            std::fflush(file_);
            std::fclose(file_);
            file_ = nullptr;
        }

        if (std::filesystem::exists(path_))
        {
            const auto to = TimeRotatedPath(current_time_slot_key_);
            std::error_code ec_rm_to;
            std::filesystem::remove(to, ec_rm_to);
            std::error_code ec_rename;
            std::filesystem::rename(path_, to, ec_rename);
        }

        PruneTimeRotatedFilesLocked();
        OpenLocked();
        current_size_ = 0;
    }

    void FileSink::RotateLocked()
    {
        if (file_ != nullptr)
        {
            std::fflush(file_);
            std::fclose(file_);
            file_ = nullptr;
        }

        if (rolling_.keep_recent_files > 0)
        {
            for (std::size_t i = rolling_.keep_recent_files; i > 0; --i)
            {
                const auto from = RotatedPath(i - 1);
                const auto to = RotatedPath(i);

                if (i == rolling_.keep_recent_files)
                {
                    std::error_code ec_remove;
                    std::filesystem::remove(to, ec_remove);
                }

                if (i - 1 == 0)
                {
                    if (std::filesystem::exists(path_))
                    {
                        std::error_code ec_rm_to;
                        std::filesystem::remove(to, ec_rm_to);
                        std::error_code ec_rename;
                        std::filesystem::rename(path_, to, ec_rename);
                    }
                }
                else
                {
                    if (std::filesystem::exists(from))
                    {
                        std::error_code ec_rm_to;
                        std::filesystem::remove(to, ec_rm_to);
                        std::error_code ec_rename;
                        std::filesystem::rename(from, to, ec_rename);
                    }
                }
            }
        }

        OpenLocked();
        current_size_ = 0;
    }

    void FileSink::Write(const std::string &line)
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (file_ == nullptr)
        {
            OpenLocked();
            if (file_ == nullptr)
            {
                return;
            }
        }

        if (rolling_.time_mode != RollingTimeMode::None)
        {
            const auto now_slot = CurrentTimeSlotKey();
            if (!current_time_slot_key_.empty() && !now_slot.empty() && now_slot != current_time_slot_key_)
            {
                RotateByTimeLocked();
                if (file_ == nullptr)
                {
                    return;
                }
            }
            current_time_slot_key_ = now_slot;
        }

        const std::size_t write_bytes = line.size() + 1;
        if (rolling_.enabled && rolling_.max_file_size_bytes > 0 &&
            current_size_ + write_bytes > rolling_.max_file_size_bytes)
        {
            RotateLocked();
            if (file_ == nullptr)
            {
                return;
            }
        }

        std::fwrite(line.data(), 1, line.size(), file_);
        std::fwrite("\n", 1, 1, file_);
        current_size_ += write_bytes;
    }

    void FileSink::Flush()
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (file_ != nullptr)
        {
            std::fflush(file_);
        }
    }

    void FileSink::UpdateRollingConfig(RollingConfigV2 rolling)
    {
        std::lock_guard<std::mutex> lk(mu_);
        rolling_ = rolling;
        current_time_slot_key_ = (rolling_.time_mode == RollingTimeMode::None) ? std::string{} : CurrentTimeSlotKey();
    }

    void DebuggerSink::Write(const std::string &line)
    {
#if defined(_WIN32)
        std::string out = line;
        out.push_back('\n');
        OutputDebugStringA(out.c_str());
#else
        (void)line;
#endif
    }

    void DebuggerSink::Flush() {}

    UdpSyslogSink::UdpSyslogSink(RemoteConfigV2 config)
        : host_(std::move(config.udp_host)),
          port_(config.udp_port),
          facility_(config.syslog_facility),
          app_name_(std::move(config.syslog_app_name)),
          hostname_(std::move(config.syslog_hostname))
    {
        if (app_name_.empty())
        {
            app_name_ = "logsys";
        }
        if (hostname_.empty())
        {
            hostname_ = "-";
        }

        if (!AcquireNetworkRuntime())
        {
            return;
        }
        network_initialized_ = true;

        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_DGRAM;
        hints.ai_protocol = IPPROTO_UDP;

        const std::string port_text = std::to_string(port_);
        addrinfo *resolved = nullptr;
        if (getaddrinfo(host_.c_str(), port_text.c_str(), &hints, &resolved) != 0)
        {
            return;
        }

        for (addrinfo *it = resolved; it != nullptr; it = it->ai_next)
        {
            if (!it->ai_addr || it->ai_addrlen <= 0)
            {
                continue;
            }

#if defined(_WIN32)
            SOCKET fd = ::socket(it->ai_family, it->ai_socktype, it->ai_protocol);
            if (fd == INVALID_SOCKET)
            {
                continue;
            }
            socket_ = static_cast<std::intptr_t>(fd);
#else
            int fd = ::socket(it->ai_family, it->ai_socktype, it->ai_protocol);
            if (fd < 0)
            {
                continue;
            }
            socket_ = static_cast<std::intptr_t>(fd);
#endif

            endpoint_.assign(reinterpret_cast<const std::uint8_t *>(it->ai_addr),
                             reinterpret_cast<const std::uint8_t *>(it->ai_addr) + it->ai_addrlen);
            endpoint_len_ = static_cast<int>(it->ai_addrlen);
            ready_ = true;
            break;
        }

        freeaddrinfo(resolved);
    }

    UdpSyslogSink::~UdpSyslogSink()
    {
        std::lock_guard<std::mutex> lk(mu_);
#if defined(_WIN32)
        if (socket_ != static_cast<std::intptr_t>(INVALID_SOCKET) && socket_ != -1)
        {
            closesocket(static_cast<SOCKET>(socket_));
            socket_ = static_cast<std::intptr_t>(INVALID_SOCKET);
        }
#else
        if (socket_ >= 0)
        {
            close(static_cast<int>(socket_));
            socket_ = -1;
        }
#endif
        ready_ = false;
        endpoint_.clear();
        endpoint_len_ = 0;
        if (network_initialized_)
        {
            ReleaseNetworkRuntime();
            network_initialized_ = false;
        }
    }

    std::uint8_t UdpSyslogSink::SeverityFromLevel(LogLevel level)
    {
        switch (level)
        {
        case LogLevel::Trace:
        case LogLevel::Debug:
            return 7; // debug
        case LogLevel::Info:
            return 6; // info
        case LogLevel::Warning:
            return 4; // warning
        case LogLevel::Error:
            return 3; // error
        case LogLevel::Fatal:
        case LogLevel::Critical:
            return 2; // critical
        }
        return 6;
    }

    std::string UdpSyslogSink::BuildPayload(const std::string &line, LogLevel level) const
    {
        const auto pri = static_cast<unsigned int>(facility_) * 8u + static_cast<unsigned int>(SeverityFromLevel(level));
        return "<" + std::to_string(pri) + ">1 - " + hostname_ + " " + app_name_ + " - - - " + line;
    }

    void UdpSyslogSink::WriteWithLevel(const std::string &line, LogLevel level)
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (!ready_ || endpoint_.empty() || endpoint_len_ <= 0)
        {
            return;
        }

        const std::string payload = BuildPayload(line, level);
        const auto *addr = reinterpret_cast<const sockaddr *>(endpoint_.data());

#if defined(_WIN32)
        const SOCKET fd = static_cast<SOCKET>(socket_);
        if (fd == INVALID_SOCKET)
        {
            return;
        }
        (void)sendto(fd, payload.data(), static_cast<int>(payload.size()), 0, addr, endpoint_len_);
#else
        const int fd = static_cast<int>(socket_);
        if (fd < 0)
        {
            return;
        }
        (void)sendto(fd, payload.data(), payload.size(), 0, addr, static_cast<socklen_t>(endpoint_len_));
#endif
    }

    void UdpSyslogSink::Write(const std::string &line)
    {
        WriteWithLevel(line, LogLevel::Info);
    }

    void UdpSyslogSink::Flush() {}

    void LevelRouter::AddDefaultSink(SinkPtr sink)
    {
        defaults_.push_back(std::move(sink));
    }

    void LevelRouter::AddLevelSink(LogLevel level, SinkPtr sink)
    {
        by_level_[level].push_back(std::move(sink));
    }

    void LevelRouter::Clear()
    {
        defaults_.clear();
        by_level_.clear();
    }

    std::vector<SinkPtr> LevelRouter::Resolve(LogLevel level) const
    {
        auto result = defaults_;
        auto it = by_level_.find(level);
        if (it != by_level_.end())
        {
            result.insert(result.end(), it->second.begin(), it->second.end());
        }
        return result;
    }

    Logger &Logger::Instance()
    {
        static Logger logger;
        return logger;
    }

    Logger::Logger()
    {
        formatter_ = std::make_shared<TextFormatter>();
        config_v2_.global_record_level = LogLevel::Info;
        config_v2_.global_output_level = LogLevel::Fatal;
        config_v2_.global_text_field_mask = kTextFieldMaskSimple;
        config_v2_.global_enable_console = true;
        config_v2_.global_enable_file = false;
        config_v2_.global_enable_debugger = false;
        StartAsyncWorker();
    }

    Logger::~Logger()
    {
        StopPeriodicFlush();
        StopAsyncWorker();
        Flush();
    }

    void Logger::SetFormatter(std::shared_ptr<IFormatter> formatter)
    {
        std::lock_guard<std::mutex> lk(mu_);
        formatter_ = std::move(formatter);
    }

    void Logger::SetLevel(LogLevel level)
    {
        // Set output threshold.
        level_.store(level, std::memory_order_relaxed);
        std::lock_guard<std::mutex> lk(mu_);
        config_v2_.global_output_level = level;
    }

    LogLevel Logger::Level() const noexcept
    {
        return level_.load(std::memory_order_relaxed);
    }

    void Logger::SetRecordLevel(LogLevel level)
    {
        record_level_.store(level, std::memory_order_relaxed);
        std::lock_guard<std::mutex> lk(mu_);
        config_v2_.global_record_level = level;
    }

    LogLevel Logger::RecordLevel() const noexcept
    {
        return record_level_.load(std::memory_order_relaxed);
    }

    void Logger::AddDefaultSink(SinkPtr sink)
    {
        std::lock_guard<std::mutex> lk(mu_);
        router_.AddDefaultSink(std::move(sink));
    }

    void Logger::AddLevelSink(LogLevel level, SinkPtr sink)
    {
        std::lock_guard<std::mutex> lk(mu_);
        router_.AddLevelSink(level, std::move(sink));
    }

    void Logger::SetFlushOnFatal(bool enabled)
    {
        flush_on_fatal_.store(enabled, std::memory_order_relaxed);
    }

    bool Logger::FlushOnFatal() const noexcept
    {
        return flush_on_fatal_.load(std::memory_order_relaxed);
    }

    void Logger::SetFatalPolicy(FatalPolicy policy)
    {
        fatal_policy_.store(static_cast<std::uint8_t>(policy), std::memory_order_relaxed);
    }

    FatalPolicy Logger::GetFatalPolicy() const noexcept
    {
        return static_cast<FatalPolicy>(fatal_policy_.load(std::memory_order_relaxed));
    }

    void Logger::SetDefaultOrigin(ErrorSource source, ModuleId module, ErrorCategory category)
    {
        default_source_.store(static_cast<std::uint8_t>(source), std::memory_order_relaxed);
        default_module_.store(static_cast<std::uint8_t>(module), std::memory_order_relaxed);
        default_category_.store(static_cast<std::uint8_t>(category), std::memory_order_relaxed);
    }

    ErrorCategory Logger::DefaultCategory() const noexcept
    {
        return static_cast<ErrorCategory>(default_category_.load(std::memory_order_relaxed));
    }

    ErrorCode Logger::DefaultCodeForLevel(LogLevel level) const
    {
        const auto source = default_source_.load(std::memory_order_relaxed);
        const auto module = default_module_.load(std::memory_order_relaxed);
        const auto detail = static_cast<std::uint16_t>(static_cast<std::uint8_t>(level) + 1);
        return MakeErrorCode(source, module, detail);
    }

    void Logger::ConfigureDefaultLogger(const DefaultLoggerOptions &options)
    {
        {
            std::lock_guard<std::mutex> lk(mu_);

            default_options_ = options;
            record_level_.store(options.record_level, std::memory_order_relaxed);
            level_.store(options.level, std::memory_order_relaxed);
            formatter_ = options.use_json_formatter ? std::static_pointer_cast<IFormatter>(std::make_shared<JsonFormatter>())
                                                    : std::static_pointer_cast<IFormatter>(std::make_shared<TextFormatter>());

            // Keep V1 APIs and V2 global config in sync.
            config_v2_.global_record_level = options.record_level;
            config_v2_.global_output_level = options.level;
            config_v2_.global_enable_console = options.enable_console;
            config_v2_.global_enable_file = options.enable_file;
            config_v2_.global_enable_debugger = options.enable_debugger;
            config_v2_.fatal_policy = FatalPolicy::FlushOnly;
            fatal_policy_.store(static_cast<std::uint8_t>(config_v2_.fatal_policy), std::memory_order_relaxed);
            config_v2_.schedule.periodic_flush_enabled = false;
            ApplyRoutingLocked();
        }

        // Default/simplified logger setup should not auto-spawn periodic tasks.
        StopPeriodicFlush();
    }

    void Logger::ConfigureSimpleLogger(LogLevel output_level, bool enable_console, bool enable_file,
                                       std::string file_path, LogLevel record_level)
    {
        DefaultLoggerOptions options;
        options.level = output_level;
        options.record_level = record_level;
        options.enable_console = enable_console;
        options.enable_file = enable_file;
        options.file_path = std::move(file_path);
        options.enable_debugger = false;
        options.use_json_formatter = false;

        ConfigureDefaultLogger(options);
        SetTextFieldMask(kTextFieldMaskSimple);
        SetAutoFillMissingMetadata(true);

        LoggerConfigV2 cfg;
        cfg.global_record_level = record_level;
        cfg.global_output_level = output_level;
        cfg.global_text_field_mask = kTextFieldMaskSimple;
        cfg.global_enable_console = enable_console;
        cfg.global_enable_file = enable_file;
        cfg.global_enable_debugger = false;
        cfg.schedule.periodic_flush_enabled = false;
        ApplyConfigV2(cfg);
    }

    void Logger::ApplyConfigV2(const LoggerConfigV2 &config)
    {
        bool enable_periodic_flush = false;
        std::chrono::milliseconds flush_interval{std::chrono::seconds(1)};
        {
            std::lock_guard<std::mutex> lk(mu_);

            config_v2_ = config;
            record_level_.store(config.global_record_level, std::memory_order_relaxed);
            level_.store(config.global_output_level, std::memory_order_relaxed);
            fatal_policy_.store(static_cast<std::uint8_t>(config.fatal_policy), std::memory_order_relaxed);
            text_field_mask_.store(config.global_text_field_mask, std::memory_order_relaxed);

            default_options_.enable_console = config.global_enable_console;
            default_options_.enable_file = config.global_enable_file;
            default_options_.enable_debugger = config.global_enable_debugger;
            ApplyRoutingLocked();

            enable_periodic_flush = config.schedule.periodic_flush_enabled;
            flush_interval = config.schedule.flush_interval;
        }

        if (!enable_periodic_flush)
        {
            StopPeriodicFlush();
            return;
        }

        const auto min_interval = std::chrono::seconds(1);
        const auto interval = flush_interval < min_interval ? min_interval : flush_interval;
        StartPeriodicFlush(interval);
    }

    const LoggerConfigV2 &Logger::CurrentConfigV2() const noexcept
    {
        return config_v2_;
    }

    bool Logger::LoadConfigV2FromJsonFile(const std::string &file_path)
    {
        std::ifstream input(file_path);
        if (!input.is_open())
        {
            return false;
        }

        std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        if (text.empty())
        {
            return false;
        }

        LoggerConfigV2 cfg;
        cfg.global_text_field_mask = config_v2_.global_text_field_mask;

        if (const auto token = ExtractJsonToken(text, "global_record_level"); token.has_value())
        {
            const auto parsed = ParseLogLevelFromToken(*token);
            if (!parsed.has_value())
            {
                return false;
            }
            cfg.global_record_level = *parsed;
        }
        if (const auto token = ExtractJsonToken(text, "global_output_level"); token.has_value())
        {
            const auto parsed = ParseLogLevelFromToken(*token);
            if (!parsed.has_value())
            {
                return false;
            }
            cfg.global_output_level = *parsed;
        }
        if (const auto token = ExtractJsonToken(text, "global_text_field_mask"); token.has_value())
        {
            cfg.global_text_field_mask = static_cast<std::uint32_t>(std::strtoul(token->c_str(), nullptr, 10));
        }
        if (const auto token = ExtractJsonToken(text, "global_enable_console"); token.has_value())
        {
            const auto parsed = ParseBoolToken(*token);
            if (!parsed.has_value())
            {
                return false;
            }
            cfg.global_enable_console = *parsed;
        }
        if (const auto token = ExtractJsonToken(text, "global_enable_file"); token.has_value())
        {
            const auto parsed = ParseBoolToken(*token);
            if (!parsed.has_value())
            {
                return false;
            }
            cfg.global_enable_file = *parsed;
        }
        if (const auto token = ExtractJsonToken(text, "global_enable_debugger"); token.has_value())
        {
            const auto parsed = ParseBoolToken(*token);
            if (!parsed.has_value())
            {
                return false;
            }
            cfg.global_enable_debugger = *parsed;
        }
        if (const auto token = ExtractJsonToken(text, "output_order"); token.has_value())
        {
            const auto parsed = ParseOutputOrderModeFromToken(*token);
            if (!parsed.has_value())
            {
                return false;
            }
            cfg.output_order = *parsed;
        }

        if (const auto body = ExtractJsonObjectBody(text, "rolling"); body.has_value())
        {
            if (const auto token = ExtractJsonToken(*body, "enabled"); token.has_value())
            {
                const auto parsed = ParseBoolToken(*token);
                if (!parsed.has_value())
                {
                    return false;
                }
                cfg.rolling.enabled = *parsed;
            }
            if (const auto token = ExtractJsonToken(*body, "max_file_size_bytes"); token.has_value())
            {
                cfg.rolling.max_file_size_bytes = static_cast<std::size_t>(std::strtoull(token->c_str(), nullptr, 10));
            }
            if (const auto token = ExtractJsonToken(*body, "keep_recent_files"); token.has_value())
            {
                cfg.rolling.keep_recent_files = static_cast<std::size_t>(std::strtoull(token->c_str(), nullptr, 10));
            }
            if (const auto token = ExtractJsonToken(*body, "time_mode"); token.has_value())
            {
                const auto parsed = ParseRollingTimeModeFromToken(*token);
                if (!parsed.has_value())
                {
                    return false;
                }
                cfg.rolling.time_mode = *parsed;
            }
        }

        if (const auto body = ExtractJsonObjectBody(text, "schedule"); body.has_value())
        {
            if (const auto token = ExtractJsonToken(*body, "periodic_flush_enabled"); token.has_value())
            {
                const auto parsed = ParseBoolToken(*token);
                if (!parsed.has_value())
                {
                    return false;
                }
                cfg.schedule.periodic_flush_enabled = *parsed;
            }
            if (const auto token = ExtractJsonToken(*body, "flush_interval_ms"); token.has_value())
            {
                cfg.schedule.flush_interval = std::chrono::milliseconds(static_cast<std::uint64_t>(std::strtoull(token->c_str(), nullptr, 10)));
            }
        }

        if (const auto body = ExtractJsonObjectBody(text, "render"); body.has_value())
        {
            if (const auto token = ExtractJsonToken(*body, "enable_color"); token.has_value())
            {
                const auto parsed = ParseBoolToken(*token);
                if (!parsed.has_value())
                {
                    return false;
                }
                cfg.render.enable_color = *parsed;
            }
            if (const auto token = ExtractJsonToken(*body, "light_theme_only"); token.has_value())
            {
                const auto parsed = ParseBoolToken(*token);
                if (!parsed.has_value())
                {
                    return false;
                }
                cfg.render.light_theme_only = *parsed;
            }
            if (const auto token = ExtractJsonToken(*body, "allow_third_party_adapter"); token.has_value())
            {
                const auto parsed = ParseBoolToken(*token);
                if (!parsed.has_value())
                {
                    return false;
                }
                cfg.render.allow_third_party_adapter = *parsed;
            }
        }

        if (const auto body = ExtractJsonObjectBody(text, "backpressure"); body.has_value())
        {
            if (const auto token = ExtractJsonToken(*body, "drop_low_level_when_full"); token.has_value())
            {
                const auto parsed = ParseBoolToken(*token);
                if (!parsed.has_value())
                {
                    return false;
                }
                cfg.backpressure.drop_low_level_when_full = *parsed;
            }
            if (const auto token = ExtractJsonToken(*body, "drop_below_level"); token.has_value())
            {
                const auto parsed = ParseLogLevelFromToken(*token);
                if (!parsed.has_value())
                {
                    return false;
                }
                cfg.backpressure.drop_below_level = *parsed;
            }
            if (const auto token = ExtractJsonToken(*body, "queue_high_watermark"); token.has_value())
            {
                cfg.backpressure.queue_high_watermark = static_cast<std::size_t>(std::strtoull(token->c_str(), nullptr, 10));
            }
        }

        if (const auto body = ExtractJsonObjectBody(text, "remote"); body.has_value())
        {
            if (const auto token = ExtractJsonToken(*body, "enable_udp_syslog"); token.has_value())
            {
                const auto parsed = ParseBoolToken(*token);
                if (!parsed.has_value())
                {
                    return false;
                }
                cfg.remote.enable_udp_syslog = *parsed;
            }

            if (const auto token = ExtractJsonToken(*body, "udp_host"); token.has_value())
            {
                std::string host = *token;
                host.erase(std::remove(host.begin(), host.end(), '"'), host.end());
                if (!host.empty())
                {
                    cfg.remote.udp_host = std::move(host);
                }
            }

            if (const auto token = ExtractJsonToken(*body, "udp_port"); token.has_value())
            {
                const auto parsed = std::strtoul(token->c_str(), nullptr, 10);
                if (parsed == 0 || parsed > 65535)
                {
                    return false;
                }
                cfg.remote.udp_port = static_cast<std::uint16_t>(parsed);
            }

            if (const auto token = ExtractJsonToken(*body, "syslog_facility"); token.has_value())
            {
                const auto parsed = std::strtoul(token->c_str(), nullptr, 10);
                if (parsed > 23)
                {
                    return false;
                }
                cfg.remote.syslog_facility = static_cast<std::uint8_t>(parsed);
            }

            if (const auto token = ExtractJsonToken(*body, "syslog_app_name"); token.has_value())
            {
                std::string app_name = *token;
                app_name.erase(std::remove(app_name.begin(), app_name.end(), '"'), app_name.end());
                if (!app_name.empty())
                {
                    cfg.remote.syslog_app_name = std::move(app_name);
                }
            }

            if (const auto token = ExtractJsonToken(*body, "syslog_hostname"); token.has_value())
            {
                std::string hostname = *token;
                hostname.erase(std::remove(hostname.begin(), hostname.end(), '"'), hostname.end());
                if (!hostname.empty())
                {
                    cfg.remote.syslog_hostname = std::move(hostname);
                }
            }
        }

        if (const auto array_body = ExtractJsonArrayBody(text, "profiles"); array_body.has_value())
        {
            const auto objects = SplitJsonObjectList(*array_body);
            for (const auto &obj : objects)
            {
                ProfileConfigV2 profile;
                if (const auto token = ExtractJsonToken(obj, "name"); token.has_value())
                {
                    profile.name = *token;
                    profile.name.erase(std::remove(profile.name.begin(), profile.name.end(), '"'), profile.name.end());
                }
                if (const auto token = ExtractJsonToken(obj, "file_path_pattern"); token.has_value())
                {
                    std::string pattern = *token;
                    pattern.erase(std::remove(pattern.begin(), pattern.end(), '"'), pattern.end());
                    profile.file_path_pattern = std::move(pattern);
                }
                if (const auto token = ExtractJsonToken(obj, "module"); token.has_value())
                {
                    std::string module_text = *token;
                    module_text.erase(std::remove_if(module_text.begin(), module_text.end(), [](unsigned char c)
                                                     { return std::isspace(c) != 0 || c == '"'; }),
                                      module_text.end());
                    if (!module_text.empty() && std::all_of(module_text.begin(), module_text.end(), [](unsigned char c)
                                                            { return std::isdigit(c) != 0 || c == '-'; }))
                    {
                        profile.module = static_cast<ModuleId>(std::atoi(module_text.c_str()));
                    }
                    else
                    {
                        const auto parsed = ParseModuleId(module_text);
                        if (!parsed.has_value())
                        {
                            return false;
                        }
                        profile.module = *parsed;
                    }
                }
                if (const auto token = ExtractJsonToken(obj, "text_field_mask"); token.has_value())
                {
                    profile.text_field_mask = static_cast<std::uint32_t>(std::strtoul(token->c_str(), nullptr, 10));
                }
                if (const auto token = ExtractJsonToken(obj, "output_level"); token.has_value())
                {
                    const auto parsed = ParseLogLevelFromToken(*token);
                    if (!parsed.has_value())
                    {
                        return false;
                    }
                    profile.output_level = *parsed;
                }
                if (const auto token = ExtractJsonToken(obj, "record_level"); token.has_value())
                {
                    const auto parsed = ParseLogLevelFromToken(*token);
                    if (!parsed.has_value())
                    {
                        return false;
                    }
                    profile.record_level = *parsed;
                }
                if (const auto token = ExtractJsonToken(obj, "enable_console"); token.has_value())
                {
                    const auto parsed = ParseBoolToken(*token);
                    if (!parsed.has_value())
                    {
                        return false;
                    }
                    profile.enable_console = *parsed;
                }
                if (const auto token = ExtractJsonToken(obj, "enable_file"); token.has_value())
                {
                    const auto parsed = ParseBoolToken(*token);
                    if (!parsed.has_value())
                    {
                        return false;
                    }
                    profile.enable_file = *parsed;
                }
                if (const auto token = ExtractJsonToken(obj, "enable_debugger"); token.has_value())
                {
                    const auto parsed = ParseBoolToken(*token);
                    if (!parsed.has_value())
                    {
                        return false;
                    }
                    profile.enable_debugger = *parsed;
                }
                if (const auto token = ExtractJsonToken(obj, "output_order"); token.has_value())
                {
                    const auto parsed = ParseOutputOrderModeFromToken(*token);
                    if (!parsed.has_value())
                    {
                        return false;
                    }
                    profile.output_order = *parsed;
                }

                cfg.profiles.push_back(std::move(profile));
            }
        }

        if (const auto token = ExtractJsonToken(text, "file_path"); token.has_value())
        {
            std::string path = *token;
            path.erase(std::remove(path.begin(), path.end(), '"'), path.end());
            std::lock_guard<std::mutex> lk(mu_);
            default_options_.file_path = std::move(path);
        }

        ApplyConfigV2(cfg);
        return true;
    }

    std::uint64_t Logger::DroppedByBackpressureCount() const noexcept
    {
        return dropped_by_backpressure_.load(std::memory_order_relaxed);
    }

    void Logger::ResetBackpressureCountersForTestOnly()
    {
        dropped_by_backpressure_.store(0, std::memory_order_relaxed);
        pending_event_count_.store(0, std::memory_order_relaxed);
    }

    bool Logger::SetLevelFromString(std::string_view level_text)
    {
        const auto parsed = ParseLogLevel(level_text);
        if (!parsed.has_value())
        {
            return false;
        }
        SetLevel(*parsed);
        return true;
    }

    bool Logger::SetLevelFromArgs(int argc, const char *const argv[], std::string_view key)
    {
        const std::string prefix = std::string(key) + "=";

        for (int i = 0; i < argc; ++i)
        {
            const std::string arg = argv[i] ? argv[i] : "";
            if (arg.rfind(prefix, 0) == 0)
            {
                return SetLevelFromString(arg.substr(prefix.size()));
            }
            if (arg == key && (i + 1) < argc && argv[i + 1] != nullptr)
            {
                return SetLevelFromString(argv[i + 1]);
            }
        }

        return false;
    }

    void Logger::SetOutputEnabled(bool enable_console, bool enable_file, bool enable_debugger)
    {
        std::lock_guard<std::mutex> lk(mu_);
        default_options_.enable_console = enable_console;
        default_options_.enable_file = enable_file;
        default_options_.enable_debugger = enable_debugger;
        config_v2_.global_enable_console = enable_console;
        config_v2_.global_enable_file = enable_file;
        config_v2_.global_enable_debugger = enable_debugger;
        ApplyRoutingLocked();
    }

    void Logger::EnableConsoleOutput(bool enabled)
    {
        std::lock_guard<std::mutex> lk(mu_);
        default_options_.enable_console = enabled;
        config_v2_.global_enable_console = enabled;
        ApplyRoutingLocked();
    }

    void Logger::EnableFileOutput(bool enabled)
    {
        std::lock_guard<std::mutex> lk(mu_);
        default_options_.enable_file = enabled;
        config_v2_.global_enable_file = enabled;
        ApplyRoutingLocked();
    }

    void Logger::EnableDebuggerOutput(bool enabled)
    {
        std::lock_guard<std::mutex> lk(mu_);
        default_options_.enable_debugger = enabled;
        config_v2_.global_enable_debugger = enabled;
        ApplyRoutingLocked();
    }

    void Logger::SetTextFieldMask(std::uint32_t mask)
    {
        text_field_mask_.store(mask, std::memory_order_relaxed);
        std::lock_guard<std::mutex> lk(mu_);
        config_v2_.global_text_field_mask = mask;
    }

    std::uint32_t Logger::TextFieldMask() const noexcept
    {
        return text_field_mask_.load(std::memory_order_relaxed);
    }

    void Logger::SetTextFieldEnabled(TextField field, bool enabled)
    {
        auto current = text_field_mask_.load(std::memory_order_relaxed);
        const auto bit = static_cast<std::uint32_t>(field);
        if (enabled)
        {
            current |= bit;
        }
        else
        {
            current &= ~bit;
        }
        text_field_mask_.store(current, std::memory_order_relaxed);
        std::lock_guard<std::mutex> lk(mu_);
        config_v2_.global_text_field_mask = current;
    }

    void Logger::SetAutoFillMissingMetadata(bool enabled)
    {
        auto_fill_missing_metadata_.store(enabled, std::memory_order_relaxed);
    }

    bool Logger::AutoFillMissingMetadata() const noexcept
    {
        return auto_fill_missing_metadata_.load(std::memory_order_relaxed);
    }

    void Logger::StartPeriodicFlush(std::chrono::milliseconds interval)
    {
        const auto min_interval = std::chrono::seconds(1);
        const auto effective_interval = interval < min_interval ? min_interval : interval;

        StopPeriodicFlush();
        periodic_flush_running_.store(true, std::memory_order_relaxed);
        periodic_flush_thread_ = std::thread([this, effective_interval]()
                                              {
                                                  while (periodic_flush_running_.load(std::memory_order_relaxed))
                                                  {
                                                      std::this_thread::sleep_for(effective_interval);
                                                      if (!periodic_flush_running_.load(std::memory_order_relaxed))
                                                      {
                                                          break;
                                                      }
                                                      this->Flush();
                                                  } });
    }

    void Logger::StopPeriodicFlush()
    {
        periodic_flush_running_.store(false, std::memory_order_relaxed);
        if (periodic_flush_thread_.joinable())
        {
            periodic_flush_thread_.join();
        }
    }

    void Logger::StartAsyncWorker()
    {
        StopAsyncWorker();
        {
            std::lock_guard<std::mutex> lk(async_mu_);
            async_worker_stop_requested_ = false;
            async_worker_processing_ = false;
            async_worker_thread_id_ = {};
        }

        async_worker_thread_ = std::thread([this]()
                                            {
                                                {
                                                    std::lock_guard<std::mutex> lk(async_mu_);
                                                    async_worker_thread_id_ = std::this_thread::get_id();
                                                }

                                                while (true)
                                                {
                                                    LogEvent event;
                                                    {
                                                        std::unique_lock<std::mutex> lk(async_mu_);
                                                        async_cv_.wait(lk, [this]
                                                                       { return async_worker_stop_requested_ || !async_queue_.empty(); });

                                                        if (async_worker_stop_requested_ && async_queue_.empty())
                                                        {
                                                            break;
                                                        }

                                                        event = std::move(async_queue_.front());
                                                        async_queue_.pop_front();
                                                        async_worker_processing_ = true;
                                                    }

                                                    pending_event_count_.fetch_sub(1, std::memory_order_relaxed);
                                                    LogEventNow(std::move(event));

                                                    {
                                                        std::lock_guard<std::mutex> lk(async_mu_);
                                                        async_worker_processing_ = false;
                                                        if (async_queue_.empty())
                                                        {
                                                            async_drain_cv_.notify_all();
                                                        }
                                                    }
                                                }

                                                {
                                                    std::lock_guard<std::mutex> lk(async_mu_);
                                                    async_worker_processing_ = false;
                                                    async_worker_thread_id_ = {};
                                                }
                                                async_drain_cv_.notify_all(); });
    }

    void Logger::StopAsyncWorker()
    {
        {
            std::lock_guard<std::mutex> lk(async_mu_);
            async_worker_stop_requested_ = true;
        }
        async_cv_.notify_all();

        if (async_worker_thread_.joinable())
        {
            async_worker_thread_.join();
        }

        {
            std::lock_guard<std::mutex> lk(async_mu_);
            async_worker_stop_requested_ = false;
            async_worker_processing_ = false;
            async_worker_thread_id_ = {};
        }
    }

    void Logger::ApplyRoutingLocked()
    {
        // Replace existing routing so dynamic output switches stay deterministic.
        router_.Clear();

        if (default_options_.enable_console)
        {
            router_.AddDefaultSink(std::make_shared<ConsoleSink>());
        }
        if (default_options_.enable_file)
        {
            router_.AddDefaultSink(std::make_shared<FileSink>(default_options_.file_path, config_v2_.rolling));
        }
        if (default_options_.enable_debugger)
        {
            router_.AddDefaultSink(std::make_shared<DebuggerSink>());
        }
        if (config_v2_.remote.enable_udp_syslog)
        {
            router_.AddDefaultSink(std::make_shared<UdpSyslogSink>(config_v2_.remote));
        }
    }

    void Logger::FlushGroupedOutputsLocked()
    {
        if (grouped_outputs_.empty())
        {
            return;
        }

        static constexpr std::array<LogLevel, 7> kOrder = {
            LogLevel::Trace, LogLevel::Debug, LogLevel::Info,
            LogLevel::Warning, LogLevel::Error, LogLevel::Fatal, LogLevel::Critical};

        for (LogLevel ordered_level : kOrder)
        {
            for (const auto &entry : grouped_outputs_)
            {
                if (entry.level != ordered_level)
                {
                    continue;
                }

                auto sinks = router_.Resolve(entry.level);
                for (const auto &sink : sinks)
                {
                    if (!sink)
                    {
                        continue;
                    }
                    if (!entry.allow_console && dynamic_cast<ConsoleSink *>(sink.get()) != nullptr)
                    {
                        continue;
                    }
                    if (!entry.allow_file && dynamic_cast<FileSink *>(sink.get()) != nullptr)
                    {
                        continue;
                    }
                    if (!entry.allow_debugger && dynamic_cast<DebuggerSink *>(sink.get()) != nullptr)
                    {
                        continue;
                    }

                    if (dynamic_cast<ConsoleSink *>(sink.get()) != nullptr)
                    {
                        sink->Write(ColorizeForConsole(entry.line, entry.level, entry.color_console && entry.light_theme_only));
                    }
                    else if (auto *udp_sink = dynamic_cast<UdpSyslogSink *>(sink.get()); udp_sink != nullptr)
                    {
                        udp_sink->WriteWithLevel(entry.line, entry.level);
                    }
                    else
                    {
                        sink->Write(entry.line);
                    }
                }
            }
        }

        grouped_outputs_.clear();
    }

    void Logger::IncrementCounters(ErrorCode code, ErrorCategory category)
    {
        const auto idx = static_cast<std::size_t>(category);
        if (idx < category_counts_.size())
        {
            category_counts_[idx].fetch_add(1, std::memory_order_relaxed);
        }

        const std::size_t start = static_cast<std::size_t>(code) % kCodeBucketCount;
        for (std::size_t probe = 0; probe < kCodeBucketCount; ++probe)
        {
            const std::size_t pos = (start + probe) % kCodeBucketCount;
            auto &bucket = code_counts_[pos];
            std::uint32_t expected = 0;
            if (bucket.key.compare_exchange_strong(expected, code, std::memory_order_relaxed))
            {
                bucket.count.store(1, std::memory_order_relaxed);
                return;
            }
            if (bucket.key.load(std::memory_order_relaxed) == code)
            {
                bucket.count.fetch_add(1, std::memory_order_relaxed);
                return;
            }
        }
    }

    std::string Logger::Sanitize(std::string message)
    {
        auto mask_exact = [&](const std::string &needle)
        {
            std::size_t pos = 0;
            while ((pos = message.find(needle, pos)) != std::string::npos)
            {
                message.replace(pos, needle.size(), "[REDACTED]");
                pos += 10;
            }
        };

        std::string lowered = message;
        std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c)
                       { return static_cast<char>(std::tolower(c)); });

        // Minimal built-in redaction to avoid accidental credential leakage.
        if (lowered.find("token") != std::string::npos)
        {
            mask_exact("token");
        }
        if (lowered.find("password") != std::string::npos)
        {
            mask_exact("password");
        }
        if (lowered.find("secret") != std::string::npos)
        {
            mask_exact("secret");
        }

        return message;
    }

    void Logger::Enqueue(LogEvent event)
    {
        BackpressureConfigV2 backpressure;
        {
            std::lock_guard<std::mutex> lk(mu_);
            backpressure = config_v2_.backpressure;
        }

        const auto pending_now = pending_event_count_.fetch_add(1, std::memory_order_relaxed) + 1;
        if (backpressure.drop_low_level_when_full &&
            pending_now > backpressure.queue_high_watermark &&
            event.level < backpressure.drop_below_level)
        {
            dropped_by_backpressure_.fetch_add(1, std::memory_order_relaxed);
            pending_event_count_.fetch_sub(1, std::memory_order_relaxed);
            return;
        }

        {
            std::lock_guard<std::mutex> lk(async_mu_);
            async_queue_.push_back(std::move(event));
        }
        async_cv_.notify_one();
    }

    void Logger::LogEventNow(LogEvent event)
    {
        if (auto_fill_missing_metadata_.load(std::memory_order_relaxed))
        {
            if (event.code == 0)
            {
                event.code = DefaultCodeForLevel(event.level);
            }
            if (event.category == ErrorCategory::Count)
            {
                event.category = DefaultCategory();
            }
            if (event.file.empty())
            {
                event.file = "<unknown>";
            }
            if (event.function.empty())
            {
                event.function = "<unknown>";
            }
            if (event.message.empty())
            {
                event.message = "<empty>";
            }
            if (event.timestamp.time_since_epoch().count() == 0)
            {
                event.timestamp = std::chrono::system_clock::now();
            }
            if (event.thread_id == 0)
            {
                event.thread_id = static_cast<std::uint64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
            }
        }

        event.text_field_mask = text_field_mask_.load(std::memory_order_relaxed);

        LogLevel resolved_record_level = record_level_.load(std::memory_order_relaxed);
        LogLevel resolved_output_level = level_.load(std::memory_order_relaxed);
        bool allow_console = true;
        bool allow_file = true;
        bool allow_debugger = true;
        OutputOrderMode output_mode = OutputOrderMode::ByTimeMixed;
        bool enable_color = false;
        bool light_theme_only = true;

        {
            std::lock_guard<std::mutex> lk(mu_);
            const auto resolved = ProfileResolverV2::Resolve(config_v2_, event.file, ModuleFromCode(event.code));
            resolved_record_level = resolved.record_level;
            resolved_output_level = resolved.output_level;
            event.text_field_mask = resolved.text_field_mask;
            allow_console = resolved.enable_console;
            allow_file = resolved.enable_file;
            allow_debugger = resolved.enable_debugger;
            output_mode = resolved.output_order;
            enable_color = config_v2_.render.enable_color;
            light_theme_only = config_v2_.render.light_theme_only;
        }

        if (event.level < resolved_record_level)
        {
            return;
        }

        // Always record accepted events, even if output is filtered out.
        IncrementCounters(event.code, event.category);

        // Output threshold applies only at sink emission stage.
        if (event.level < resolved_output_level)
        {
            return;
        }

        std::shared_ptr<IFormatter> formatter;
        {
            std::lock_guard<std::mutex> lk(mu_);
            formatter = formatter_;
        }

        const std::string line = formatter ? formatter->Format(event) : event.message;
        if (output_mode == OutputOrderMode::ByLevelGrouped)
        {
            std::lock_guard<std::mutex> lk(mu_);
            grouped_outputs_.push_back(PendingOutputV2{
                event.level,
                line,
                allow_console,
                allow_file,
                allow_debugger,
                enable_color,
                light_theme_only});

            if (grouped_outputs_.size() >= 64)
            {
                FlushGroupedOutputsLocked();
            }
            return;
        }

        std::vector<SinkPtr> sinks;
        {
            std::lock_guard<std::mutex> lk(mu_);
            sinks = router_.Resolve(event.level);
        }

        for (const auto &sink : sinks)
        {
            if (!sink)
            {
                continue;
            }
            if (!allow_console && dynamic_cast<ConsoleSink *>(sink.get()) != nullptr)
            {
                continue;
            }
            if (!allow_file && dynamic_cast<FileSink *>(sink.get()) != nullptr)
            {
                continue;
            }
            if (!allow_debugger && dynamic_cast<DebuggerSink *>(sink.get()) != nullptr)
            {
                continue;
            }

            if (dynamic_cast<ConsoleSink *>(sink.get()) != nullptr)
            {
                sink->Write(ColorizeForConsole(line, event.level, enable_color && light_theme_only));
            }
            else if (auto *udp_sink = dynamic_cast<UdpSyslogSink *>(sink.get()); udp_sink != nullptr)
            {
                udp_sink->WriteWithLevel(line, event.level);
            }
            else
            {
                sink->Write(line);
            }
        }

        if (event.level == LogLevel::Fatal && FlushOnFatal())
        {
            Flush();
            if (GetFatalPolicy() == FatalPolicy::AbortAfterFlush)
            {
                std::abort();
            }
        }
    }

    std::uint64_t Logger::GetErrorCountByCode(ErrorCode code) const
    {
        const std::size_t start = static_cast<std::size_t>(code) % kCodeBucketCount;
        for (std::size_t probe = 0; probe < kCodeBucketCount; ++probe)
        {
            const std::size_t pos = (start + probe) % kCodeBucketCount;
            const auto &bucket = code_counts_[pos];
            const auto key = bucket.key.load(std::memory_order_relaxed);
            if (key == code)
            {
                return bucket.count.load(std::memory_order_relaxed);
            }
            if (key == 0)
            {
                return 0;
            }
        }
        return 0;
    }

    std::uint64_t Logger::GetErrorCountByCategory(ErrorCategory category) const
    {
        const auto idx = static_cast<std::size_t>(category);
        if (idx >= category_counts_.size())
        {
            return 0;
        }
        return category_counts_[idx].load(std::memory_order_relaxed);
    }

    bool Logger::ResetErrorCountersForTestOnly()
    {
        const char *allow = std::getenv("LOGSYS_ALLOW_TEST_API");
        if (allow == nullptr || std::string(allow) != "1")
        {
            return false;
        }

        for (auto &c : category_counts_)
        {
            c.store(0, std::memory_order_relaxed);
        }
        for (auto &bucket : code_counts_)
        {
            bucket.key.store(0, std::memory_order_relaxed);
            bucket.count.store(0, std::memory_order_relaxed);
        }

        return true;
    }

    void Logger::Flush()
    {
        {
            std::unique_lock<std::mutex> lk(async_mu_);
            if (std::this_thread::get_id() != async_worker_thread_id_)
            {
                async_drain_cv_.wait(lk, [this]
                                     { return async_queue_.empty() && !async_worker_processing_; });
            }
        }

        std::vector<SinkPtr> sinks;
        {
            std::lock_guard<std::mutex> lk(mu_);
            FlushGroupedOutputsLocked();
            sinks = router_.Resolve(LogLevel::Info);
        }
        for (const auto &sink : sinks)
        {
            if (sink)
            {
                sink->Flush();
            }
        }
    }

    LogStreamBuilder::LogStreamBuilder(Logger &logger, LogLevel level, ErrorCode code, ErrorCategory category,
                                       const char *file, std::uint32_t line, const char *function)
        : logger_(logger)
    {
        event_.timestamp = std::chrono::system_clock::now();
        event_.level = level;
        event_.code = code;
        event_.category = category;
        event_.file = file ? file : "";
        event_.line = line;
        event_.function = function ? function : "";
        event_.thread_id = static_cast<std::uint64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
    }

    LogStreamBuilder::~LogStreamBuilder()
    {
        event_.message = Logger::Sanitize(ss_.str());
        logger_.Enqueue(std::move(event_));
    }

    LogStreamBuilder &LogStreamBuilder::SetField(std::string key, std::string value)
    {
        event_.SetField(std::move(key), std::move(value));
        return *this;
    }

} // namespace logsys
