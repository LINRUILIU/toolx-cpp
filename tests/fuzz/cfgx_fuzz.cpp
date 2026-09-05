#include "cfgx.h"
#include <cstddef>
#include <cstdint>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    if (size > 4096)
        return 0;
    const std::string_view text(reinterpret_cast<const char*>(data), size);
    (void)cfgx::ParseJson(text);
    (void)cfgx::ParsePath(text);
    return 0;
}
