#include "utils.h"

#include <filesystem>
#include <iostream>

int main()
{
    // Scenario 1: string helpers keep whitespace and case rules local.
    const auto trimmed = utils::str::trim("  ToolX  ");
    const auto parts = utils::str::split("a,,b", ',', true);
    std::cout << "trimmed=" << trimmed << " parts=" << parts.size() << " lower=" << utils::str::to_lower_ascii("HTTP")
              << "\n";

    // Scenario 2: parsing helpers never throw; failures are explicit values.
    const auto integer = utils::parse::parse_int32("42");
    const auto bad_bool = utils::parse::parse_bool("maybe");
    std::cout << "int=" << integer.value << " bool-ok=" << bad_bool.ok << "\n";

    // Scenario 3: UTF-8 display width is separate from byte length.
    std::cout << "utf8-bytes=" << utils::str::measure_text_utf8_strlen("A")
              << " width=" << utils::str::measure_text_utf8_display_width("A") << "\n";

    // Scenario 4: path helpers are small, predictable wrappers.
    const auto root = std::filesystem::current_path() / "temp" / "toolx_utils_cookbook" / "nested" / "file.txt";
    const auto status = utils::path::ensure_parent_dir(root.string());
    std::cout << "parent-ok=" << status.ok << " path=" << utils::path::normalize_slash(root.string()).size() << "\n";
    std::error_code ec;
    std::filesystem::remove_all(root.parent_path().parent_path(), ec);
    return 0;
}
