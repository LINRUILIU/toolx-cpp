#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace utils
{

    template <typename T>
    struct ParseResult
    {
        bool ok{false};
        T value{};
        std::string error;
    }; // 解析结果结构体，包含操作是否成功、解析值以及错误信息等字段。

    struct Status
    {
        bool ok{false};
        std::string error;
    }; // 状态结构体，包含操作是否成功以及错误信息等字段。

    namespace err
    {

        std::string join_context(std::string_view scope,
                                 std::string_view key,
                                 std::string_view reason); // 连接上下文信息方法，参数为作用域、键以及原因，返回格式化后的错误信息字符串。

        std::string format_error(std::string_view scope,
                                 std::string_view key,
                                 std::string_view reason); // 格式化错误信息方法，参数为作用域、键以及原因，返回格式化后的错误信息字符串。

    } // namespace err

    namespace str
    {

        std::string ltrim(std::string_view input); // 左侧修剪方法，参数为输入字符串，返回去除左侧空白字符后的字符串。
        std::string rtrim(std::string_view input); // 右侧修剪方法，参数为输入字符串，返回去除右侧空白字符后的字符串。
        std::string trim(std::string_view input);  // 修剪方法，参数为输入字符串，返回去除两侧空白字符后的字符串。

        std::vector<std::string> split(std::string_view input,
                                       char delimiter,
                                       bool skip_empty = false); // 分割字符串方法，参数为输入字符串、分隔符以及是否跳过空字符串，返回分割后的字符串列表。

        std::string to_lower_ascii(std::string_view input); // 转换为小写ASCII方法，参数为输入字符串，返回转换为小写ASCII后的字符串。

        bool iequals(std::string_view lhs, std::string_view rhs);         // 不区分大小写比较方法，参数为两个字符串，返回是否相等。
        bool starts_with(std::string_view text, std::string_view prefix); // 前缀检查方法，参数为文本和前缀，返回文本是否以指定前缀开头。
        bool ends_with(std::string_view text, std::string_view suffix);   // 后缀检查方法，参数为文本和后缀，返回文本是否以指定后缀结尾。

        std::size_t measure_text_utf8_strlen(std::string_view text) noexcept;
        std::size_t measure_text_utf8_codepoints(std::string_view text) noexcept;
        std::size_t measure_text_utf8_display_width(std::string_view text) noexcept;
        std::size_t measure_text_gbk_strlen(std::string_view text) noexcept;
        std::size_t measure_text_gbk_codepoints(std::string_view text) noexcept;
        std::size_t measure_text_gbk_display_width(std::string_view text) noexcept;

    } // namespace str

    namespace time
    {

        std::int64_t now_system_ms(); // 获取当前系统时间戳方法，返回当前时间的毫秒级时间戳。

        std::string format_local_timestamp_ms(std::int64_t epoch_ms); // 格式化本地时间戳方法，参数为毫秒级时间戳，返回格式化后的本地时间字符串。

        std::int64_t steady_elapsed_ms(std::chrono::steady_clock::time_point start); // 计算稳态时间差方法，参数为起始时间点，返回从起始时间点到当前时间的毫秒数。

    } // namespace time

    namespace parse
    {

        ParseResult<std::int32_t> parse_int32(std::string_view text); // 解析32位整数方法，参数为输入字符串，返回解析结果，包括解析值和错误信息。
        ParseResult<double> parse_double(std::string_view text);      // 解析双精度浮点数方法，参数为输入字符串，返回解析结果，包括解析值和错误信息。
        ParseResult<bool> parse_bool(std::string_view text);          // 解析布尔值方法，参数为输入字符串，返回解析结果，包括解析值和错误信息。

    } // namespace parse

    namespace path
    {

        std::string normalize_slash(std::string_view input);  // 规范化路径分隔符方法，参数为输入路径字符串，返回将反斜杠替换为正斜杠后的路径字符串。
        bool file_exists(std::string_view file_path);         // 文件存在检查方法，参数为文件路径字符串，返回文件是否存在。
        Status ensure_parent_dir(std::string_view file_path); // 确保父目录存在方法，参数为文件路径字符串，返回操作状态，包括是否成功以及错误信息。

    } // namespace path

    namespace hash
    {

        struct Fnv1a32State
        {
            std::uint32_t value{2166136261u};
            void reset() noexcept;
            void update(const void *data, std::size_t size) noexcept;
            std::uint32_t final() const noexcept;
        };

        struct Fnv1a64State
        {
            std::uint64_t value{14695981039346656037ull};
            void reset() noexcept;
            void update(const void *data, std::size_t size) noexcept;
            std::uint64_t final() const noexcept;
        };

        struct Crc32State
        {
            std::uint32_t value{0xFFFFFFFFu};
            void reset() noexcept;
            void update(const void *data, std::size_t size) noexcept;
            std::uint32_t final() const noexcept;
        };

        struct Adler32State
        {
            std::uint32_t a{1u};
            std::uint32_t b{0u};
            void reset() noexcept;
            void update(const void *data, std::size_t size) noexcept;
            std::uint32_t final() const noexcept;
        };

        std::uint32_t fnv1a32_bytes(const void *data, std::size_t size) noexcept;
        std::uint64_t fnv1a64_bytes(const void *data, std::size_t size) noexcept;
        std::uint32_t crc32_bytes(const void *data, std::size_t size) noexcept;
        std::uint32_t adler32_bytes(const void *data, std::size_t size) noexcept;

        std::uint32_t fnv1a32(std::string_view text) noexcept;
        std::uint64_t fnv1a64(std::string_view text) noexcept;
        std::uint32_t crc32(std::string_view text) noexcept;
        std::uint32_t adler32(std::string_view text) noexcept;

    } // namespace hash

} // namespace utils
