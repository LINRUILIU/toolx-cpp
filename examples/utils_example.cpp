#include <chrono>
#include <iostream>
#include <string>

#include "utils.h"

int main()
{
    const auto now_ms = utils::time::now_system_ms();
    std::cout << "now: " << utils::time::format_local_timestamp_ms(now_ms) << '\n';

    const auto tokens = utils::str::split("  A,B,,C  ", ',', false);
    std::cout << "tokens=" << tokens.size() << '\n';

    const auto int_result = utils::parse::parse_int32("42");
    if (!int_result.ok)
    {
        std::cout << int_result.error << '\n';
        return 2;
    }

    const auto bool_result = utils::parse::parse_bool("YES");
    if (!bool_result.ok)
    {
        std::cout << bool_result.error << '\n';
        return 2;
    }

    const auto dir_status = utils::path::ensure_parent_dir("temp/example/out.txt");
    if (!dir_status.ok)
    {
        std::cout << dir_status.error << '\n';
        return 2;
    }

    const std::string utf8_text = std::string("A") + "\xE4\xB8\xAD";
    const auto strlen_bytes = utils::str::measure_text_utf8_strlen(utf8_text);
    const auto codepoints = utils::str::measure_text_utf8_codepoints(utf8_text);
    const auto display_width = utils::str::measure_text_utf8_display_width(utf8_text);

    const std::string gbk_text = std::string("A") + "\xD6\xD0" + "B";
    const auto gbk_strlen = utils::str::measure_text_gbk_strlen(gbk_text);
    const auto gbk_codepoints = utils::str::measure_text_gbk_codepoints(gbk_text);
    const auto gbk_display_width = utils::str::measure_text_gbk_display_width(gbk_text);

    const auto h32 = utils::hash::fnv1a32("hello");

    const auto start = std::chrono::steady_clock::now();
    const auto elapsed = utils::time::steady_elapsed_ms(start);
    std::cout << "int=" << int_result.value << ", bool=" << bool_result.value
              << ", elapsed=" << elapsed << "ms\n";
    std::cout << "strlen=" << strlen_bytes
              << " codepoints=" << codepoints
              << " width=" << display_width
              << " hash32=0x" << std::hex << h32 << std::dec << "\n";
    std::cout << "gbk_strlen=" << gbk_strlen
              << " gbk_codepoints=" << gbk_codepoints
              << " gbk_width=" << gbk_display_width << "\n";

    return 0;
}
