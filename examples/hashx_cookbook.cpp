#include "hashx.h"

#include <array>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string_view>

int main()
{
    const std::string_view text = "toolx";

    // Scenario 1: one-shot text hashes are convenient for cache keys.
    std::cout << std::hex << "fnv32=" << hashx::fnv1a32(text) << " crc32=" << hashx::crc32(text) << "\n";

    // Scenario 2: bytes APIs make binary buffers explicit.
    const std::array<std::uint8_t, 4> bytes{{0x54, 0x6f, 0x6f, 0x6c}};
    std::cout << "adler-bytes=" << hashx::adler32_bytes(bytes.data(), bytes.size()) << "\n";

    // Scenario 3: streaming state supports chunked file or network input.
    hashx::Fnv1a64State state;
    state.update("to", 2);
    state.update("olx", 3);
    std::cout << "stream-fnv64=" << state.final() << "\n";

    // Scenario 4: reset makes state reusable without reallocating.
    state.reset();
    state.update(text.data(), text.size());
    std::cout << "reset-fnv64=" << state.final() << "\n";
    return 0;
}
