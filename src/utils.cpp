#include "utils.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <sstream>

namespace utils
{
    namespace
    {

        bool is_ascii_space(char c)
        {
            switch (c)
            {
            case ' ':
            case '\t':
            case '\n':
            case '\r':
            case '\f':
            case '\v':
                return true;
            default:
                return false;
            }
        }

        constexpr std::uint32_t k_fnv32_offset = 2166136261u;
        constexpr std::uint32_t k_fnv32_prime = 16777619u;

        constexpr std::uint64_t k_fnv64_offset = 14695981039346656037ull;
        constexpr std::uint64_t k_fnv64_prime = 1099511628211ull;

        constexpr std::uint32_t k_adler_mod = 65521u;

        std::array<std::uint32_t, 256> build_crc32_table()
        {
            std::array<std::uint32_t, 256> table{};
            for (std::uint32_t i = 0; i < 256; ++i)
            {
                std::uint32_t c = i;
                for (int bit = 0; bit < 8; ++bit)
                {
                    if ((c & 1u) != 0u)
                    {
                        c = 0xEDB88320u ^ (c >> 1u);
                    }
                    else
                    {
                        c >>= 1u;
                    }
                }
                table[i] = c;
            }
            return table;
        }

        const std::array<std::uint32_t, 256> &crc32_table()
        {
            static const std::array<std::uint32_t, 256> table = build_crc32_table();
            return table;
        }

        bool is_utf8_cont(unsigned char b)
        {
            return (b & 0xC0U) == 0x80U;
        }

        std::size_t display_width_for_codepoint(std::uint32_t cp);

        struct Utf8ScanResult
        {
            std::size_t codepoints{0};
            std::size_t display_width{0};
        };

        struct GbkScanResult
        {
            std::size_t codepoints{0};
            std::size_t display_width{0};
        };

        Utf8ScanResult scan_utf8_metrics(std::string_view text) noexcept
        {
            Utf8ScanResult out;
            std::size_t i = 0;
            while (i < text.size())
            {
                const auto b0 = static_cast<unsigned char>(text[i]);
                if (b0 <= 0x7FU)
                {
                    ++out.codepoints;
                    out.display_width += display_width_for_codepoint(static_cast<std::uint32_t>(b0));
                    ++i;
                    continue;
                }

                std::size_t cont_count = 0;
                std::uint32_t cp = 0;

                if ((b0 & 0xE0U) == 0xC0U)
                {
                    cont_count = 1;
                    cp = b0 & 0x1FU;
                }
                else if ((b0 & 0xF0U) == 0xE0U)
                {
                    cont_count = 2;
                    cp = b0 & 0x0FU;
                }
                else if ((b0 & 0xF8U) == 0xF0U)
                {
                    cont_count = 3;
                    cp = b0 & 0x07U;
                }
                else
                {
                    ++out.codepoints;
                    out.display_width += 1;
                    ++i;
                    continue;
                }

                if (i + cont_count >= text.size())
                {
                    ++out.codepoints;
                    out.display_width += 1;
                    ++i;
                    continue;
                }

                bool valid = true;
                for (std::size_t j = 1; j <= cont_count; ++j)
                {
                    const auto cb = static_cast<unsigned char>(text[i + j]);
                    if (!is_utf8_cont(cb))
                    {
                        valid = false;
                        break;
                    }
                    cp = static_cast<std::uint32_t>((cp << 6U) | (cb & 0x3FU));
                }

                if (!valid)
                {
                    ++out.codepoints;
                    out.display_width += 1;
                    ++i;
                    continue;
                }

                ++out.codepoints;
                out.display_width += display_width_for_codepoint(cp);
                i += cont_count + 1;
            }

            return out;
        }

        bool is_gbk_lead(unsigned char b) noexcept
        {
            return b >= 0x81U && b <= 0xFEU;
        }

        bool is_gbk_trail(unsigned char b) noexcept
        {
            return (b >= 0x40U && b <= 0xFEU && b != 0x7FU);
        }

        std::size_t display_width_for_ascii(unsigned char b) noexcept
        {
            if (b < 0x20U || b == 0x7FU)
            {
                return 0;
            }
            return 1;
        }

        GbkScanResult scan_gbk_metrics(std::string_view text) noexcept
        {
            GbkScanResult out;
            std::size_t i = 0;
            while (i < text.size())
            {
                const auto b0 = static_cast<unsigned char>(text[i]);
                if (b0 <= 0x7FU)
                {
                    ++out.codepoints;
                    out.display_width += display_width_for_ascii(b0);
                    ++i;
                    continue;
                }

                if (is_gbk_lead(b0) && (i + 1) < text.size())
                {
                    const auto b1 = static_cast<unsigned char>(text[i + 1]);
                    if (is_gbk_trail(b1))
                    {
                        ++out.codepoints;
                        out.display_width += 2;
                        i += 2;
                        continue;
                    }
                }

                ++out.codepoints;
                out.display_width += 1;
                ++i;
            }

            return out;
        }

        bool is_cjk_wide_codepoint(std::uint32_t cp)
        {
            return (cp >= 0x1100 && cp <= 0x115F) ||
                   (cp >= 0x2329 && cp <= 0x232A) ||
                   (cp >= 0x2E80 && cp <= 0xA4CF) ||
                   (cp >= 0xAC00 && cp <= 0xD7A3) ||
                   (cp >= 0xF900 && cp <= 0xFAFF) ||
                   (cp >= 0xFE10 && cp <= 0xFE19) ||
                   (cp >= 0xFE30 && cp <= 0xFE6F) ||
                   (cp >= 0xFF00 && cp <= 0xFF60) ||
                   (cp >= 0xFFE0 && cp <= 0xFFE6) ||
                   (cp >= 0x1F300 && cp <= 0x1FAFF) ||
                   (cp >= 0x20000 && cp <= 0x3FFFD);
        }

        std::size_t display_width_for_codepoint(std::uint32_t cp)
        {
            if (cp == 0)
            {
                return 0;
            }
            if (cp < 0x20 || (cp >= 0x7F && cp < 0xA0))
            {
                return 0;
            }
            return is_cjk_wide_codepoint(cp) ? 2U : 1U;
        }

    } // namespace

    namespace err
    {

        std::string join_context(std::string_view scope,
                                 std::string_view key,
                                 std::string_view reason)
        {
            std::string out;
            out.reserve(scope.size() + key.size() + reason.size() + 24);
            out += "[scope=";
            out.append(scope);
            out += "] [key=";
            out.append(key);
            out += "] ";
            out.append(reason);
            return out;
        }

        std::string format_error(std::string_view scope,
                                 std::string_view key,
                                 std::string_view reason)
        {
            return join_context(scope, key, reason);
        }

    } // namespace err

    namespace str
    {

        std::string ltrim(std::string_view input)
        {
            std::size_t i = 0;
            while (i < input.size() && is_ascii_space(input[i]))
            {
                ++i;
            }
            return std::string(input.substr(i));
        }

        std::string rtrim(std::string_view input)
        {
            std::size_t i = input.size();
            while (i > 0 && is_ascii_space(input[i - 1]))
            {
                --i;
            }
            return std::string(input.substr(0, i));
        }

        std::string trim(std::string_view input)
        {
            std::size_t begin = 0;
            while (begin < input.size() && is_ascii_space(input[begin]))
            {
                ++begin;
            }

            std::size_t end = input.size();
            while (end > begin && is_ascii_space(input[end - 1]))
            {
                --end;
            }

            return std::string(input.substr(begin, end - begin));
        }

        std::vector<std::string> split(std::string_view input,
                                       char delimiter,
                                       bool skip_empty)
        {
            std::vector<std::string> parts;
            std::size_t start = 0;

            for (std::size_t i = 0; i <= input.size(); ++i)
            {
                if (i == input.size() || input[i] == delimiter)
                {
                    std::string piece(input.substr(start, i - start));
                    if (!skip_empty || !piece.empty())
                    {
                        parts.push_back(std::move(piece));
                    }
                    start = i + 1;
                }
            }

            return parts;
        }

        std::string to_lower_ascii(std::string_view input)
        {
            std::string out;
            out.reserve(input.size());
            for (char c : input)
            {
                if (c >= 'A' && c <= 'Z')
                {
                    out.push_back(static_cast<char>(c - 'A' + 'a'));
                }
                else
                {
                    out.push_back(c);
                }
            }
            return out;
        }

        bool iequals(std::string_view lhs, std::string_view rhs)
        {
            if (lhs.size() != rhs.size())
            {
                return false;
            }

            for (std::size_t i = 0; i < lhs.size(); ++i)
            {
                const char a = lhs[i];
                const char b = rhs[i];
                const char al = (a >= 'A' && a <= 'Z') ? static_cast<char>(a - 'A' + 'a') : a;
                const char bl = (b >= 'A' && b <= 'Z') ? static_cast<char>(b - 'A' + 'a') : b;
                if (al != bl)
                {
                    return false;
                }
            }

            return true;
        }

        bool starts_with(std::string_view text, std::string_view prefix)
        {
            return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
        }

        bool ends_with(std::string_view text, std::string_view suffix)
        {
            return text.size() >= suffix.size() &&
                   text.substr(text.size() - suffix.size(), suffix.size()) == suffix;
        }

        std::size_t measure_text_utf8_strlen(std::string_view text) noexcept
        {
            std::size_t c_len = 0;
            while (c_len < text.size() && text[c_len] != '\0')
            {
                ++c_len;
            }
            return c_len;
        }

        std::size_t measure_text_utf8_codepoints(std::string_view text) noexcept
        {
            return scan_utf8_metrics(text).codepoints;
        }

        std::size_t measure_text_utf8_display_width(std::string_view text) noexcept
        {
            return scan_utf8_metrics(text).display_width;
        }

        std::size_t measure_text_gbk_strlen(std::string_view text) noexcept
        {
            return measure_text_utf8_strlen(text);
        }

        std::size_t measure_text_gbk_codepoints(std::string_view text) noexcept
        {
            return scan_gbk_metrics(text).codepoints;
        }

        std::size_t measure_text_gbk_display_width(std::string_view text) noexcept
        {
            return scan_gbk_metrics(text).display_width;
        }

    } // namespace str

    namespace time
    {

        std::int64_t now_system_ms()
        {
            const auto now = std::chrono::system_clock::now();
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                       now.time_since_epoch())
                .count();
        }

        std::string format_local_timestamp_ms(std::int64_t epoch_ms)
        {
            std::int64_t sec = epoch_ms / 1000;
            std::int64_t ms = epoch_ms % 1000;
            if (ms < 0)
            {
                ms += 1000;
                --sec;
            }

            const auto tt = static_cast<std::time_t>(sec);

            std::tm tmv{};
#if defined(_WIN32)
            if (localtime_s(&tmv, &tt) != 0)
            {
                return "1970-01-01 00:00:00.000";
            }
#else
            if (localtime_r(&tt, &tmv) == nullptr)
            {
                return "1970-01-01 00:00:00.000";
            }
#endif

            std::ostringstream os;
            os << std::put_time(&tmv, "%Y-%m-%d %H:%M:%S")
               << '.' << std::setw(3) << std::setfill('0') << ms;
            return os.str();
        }

        std::int64_t steady_elapsed_ms(std::chrono::steady_clock::time_point start)
        {
            const auto now = std::chrono::steady_clock::now();
            if (start > now)
            {
                return 0;
            }

            return std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
        }

    } // namespace time

    namespace parse
    {

        ParseResult<std::int32_t> parse_int32(std::string_view text)
        {
            ParseResult<std::int32_t> out;
            std::int32_t value = 0;

            const std::string trimmed = str::trim(text);
            if (trimmed.empty())
            {
                out.error = err::join_context("parse", "int32", "empty input");
                return out;
            }

            const auto *begin = trimmed.data();
            const auto *end = trimmed.data() + trimmed.size();
            const auto result = std::from_chars(begin, end, value);
            if (result.ec == std::errc::result_out_of_range)
            {
                out.error = err::join_context("parse", "int32", "integer out of range");
                return out;
            }
            if (result.ec != std::errc() || result.ptr != end)
            {
                out.error = err::join_context("parse", "int32", "invalid integer token");
                return out;
            }

            out.ok = true;
            out.value = value;
            return out;
        }

        ParseResult<double> parse_double(std::string_view text)
        {
            ParseResult<double> out;

            std::string copy = str::trim(text);
            if (copy.empty())
            {
                out.error = err::join_context("parse", "double", "empty input");
                return out;
            }

            char *parse_end = nullptr;
            errno = 0;
            const double value = std::strtod(copy.c_str(), &parse_end);

            if (parse_end == nullptr || parse_end == copy.c_str() || *parse_end != '\0')
            {
                out.error = err::join_context("parse", "double", "invalid floating-point token");
                return out;
            }

            if (errno == ERANGE)
            {
                out.error = err::join_context("parse", "double", "value out of range");
                return out;
            }

            if (!std::isfinite(value))
            {
                out.error = err::join_context("parse", "double", "non-finite values are not allowed");
                return out;
            }

            out.ok = true;
            out.value = value;
            return out;
        }

        ParseResult<bool> parse_bool(std::string_view text)
        {
            ParseResult<bool> out;
            const std::string lower = str::to_lower_ascii(str::trim(text));

            if (lower == "1" || lower == "true" || lower == "yes" || lower == "on")
            {
                out.ok = true;
                out.value = true;
                return out;
            }

            if (lower == "0" || lower == "false" || lower == "no" || lower == "off")
            {
                out.ok = true;
                out.value = false;
                return out;
            }

            out.error = err::join_context("parse", "bool", "expected true/false/1/0/yes/no/on/off");
            return out;
        }

    } // namespace parse

    namespace path
    {

        std::string normalize_slash(std::string_view input)
        {
            std::string out(input);
            std::replace(out.begin(), out.end(), '\\', '/');
            return out;
        }

        bool file_exists(std::string_view file_path)
        {
            if (str::trim(file_path).empty())
            {
                return false;
            }

            std::error_code ec;
            const std::filesystem::path p{std::string(file_path)};
            return std::filesystem::is_regular_file(p, ec) && !ec;
        }

        Status ensure_parent_dir(std::string_view file_path)
        {
            Status st;

            if (str::trim(file_path).empty())
            {
                st.error = err::join_context("path", "ensure_parent_dir", "empty file path");
                return st;
            }

            const std::filesystem::path p{std::string(file_path)};
            const auto parent = p.parent_path();
            if (parent.empty())
            {
                st.ok = true;
                return st;
            }

            std::error_code ec;
            std::filesystem::create_directories(parent, ec);
            if (ec)
            {
                st.error = err::join_context("path", "ensure_parent_dir", ec.message());
                return st;
            }

            st.ok = true;
            return st;
        }

    } // namespace path

    namespace hash
    {

        void Fnv1a32State::reset() noexcept
        {
            value = k_fnv32_offset;
        }

        void Fnv1a32State::update(const void *data, std::size_t size) noexcept
        {
            const auto *p = static_cast<const unsigned char *>(data);
            for (std::size_t i = 0; i < size; ++i)
            {
                value ^= static_cast<std::uint32_t>(p[i]);
                value *= k_fnv32_prime;
            }
        }

        std::uint32_t Fnv1a32State::final() const noexcept
        {
            return value;
        }

        void Fnv1a64State::reset() noexcept
        {
            value = k_fnv64_offset;
        }

        void Fnv1a64State::update(const void *data, std::size_t size) noexcept
        {
            const auto *p = static_cast<const unsigned char *>(data);
            for (std::size_t i = 0; i < size; ++i)
            {
                value ^= static_cast<std::uint64_t>(p[i]);
                value *= k_fnv64_prime;
            }
        }

        std::uint64_t Fnv1a64State::final() const noexcept
        {
            return value;
        }

        void Crc32State::reset() noexcept
        {
            value = 0xFFFFFFFFu;
        }

        void Crc32State::update(const void *data, std::size_t size) noexcept
        {
            const auto *p = static_cast<const unsigned char *>(data);
            const auto &table = crc32_table();
            for (std::size_t i = 0; i < size; ++i)
            {
                const std::uint32_t idx = (value ^ static_cast<std::uint32_t>(p[i])) & 0xFFu;
                value = table[idx] ^ (value >> 8u);
            }
        }

        std::uint32_t Crc32State::final() const noexcept
        {
            return value ^ 0xFFFFFFFFu;
        }

        void Adler32State::reset() noexcept
        {
            a = 1u;
            b = 0u;
        }

        void Adler32State::update(const void *data, std::size_t size) noexcept
        {
            const auto *p = static_cast<const unsigned char *>(data);
            for (std::size_t i = 0; i < size; ++i)
            {
                a = (a + static_cast<std::uint32_t>(p[i])) % k_adler_mod;
                b = (b + a) % k_adler_mod;
            }
        }

        std::uint32_t Adler32State::final() const noexcept
        {
            return (b << 16u) | a;
        }

        std::uint32_t fnv1a32_bytes(const void *data, std::size_t size) noexcept
        {
            Fnv1a32State st;
            st.update(data, size);
            return st.final();
        }

        std::uint64_t fnv1a64_bytes(const void *data, std::size_t size) noexcept
        {
            Fnv1a64State st;
            st.update(data, size);
            return st.final();
        }

        std::uint32_t crc32_bytes(const void *data, std::size_t size) noexcept
        {
            Crc32State st;
            st.update(data, size);
            return st.final();
        }

        std::uint32_t adler32_bytes(const void *data, std::size_t size) noexcept
        {
            Adler32State st;
            st.update(data, size);
            return st.final();
        }

        std::uint32_t fnv1a32(std::string_view text) noexcept
        {
            return fnv1a32_bytes(text.data(), text.size());
        }

        std::uint64_t fnv1a64(std::string_view text) noexcept
        {
            return fnv1a64_bytes(text.data(), text.size());
        }

        std::uint32_t crc32(std::string_view text) noexcept
        {
            return crc32_bytes(text.data(), text.size());
        }

        std::uint32_t adler32(std::string_view text) noexcept
        {
            return adler32_bytes(text.data(), text.size());
        }

    } // namespace hash

} // namespace utils
