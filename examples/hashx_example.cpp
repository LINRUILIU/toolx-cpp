#include <cstdint>
#include <iomanip>
#include <iostream>

#include "hashx.h"
#include "utils.h"

int main()
{
    constexpr const char *text = "hello";

    const std::uint32_t h32 = hashx::fnv1a32(text);
    const std::uint64_t h64 = hashx::fnv1a64(text);
    const std::uint32_t crc = hashx::crc32(text);
    const std::uint32_t adler = hashx::adler32(text);
    const std::uint32_t h32_utils = utils::hash::fnv1a32(text);

    hashx::Fnv1a32State stream;
    stream.update(text, 2);
    stream.update(text + 2, 3);

    std::cout << "text=" << text << '\n';
    std::cout << "fnv1a32=0x" << std::hex << h32 << '\n';
    std::cout << "fnv1a32(stream)=0x" << std::hex << stream.final() << '\n';
    std::cout << "fnv1a32(utils)=0x" << std::hex << h32_utils << '\n';
    std::cout << "fnv1a64=0x" << std::hex << h64 << '\n';
    std::cout << "crc32=0x" << std::hex << crc << '\n';
    std::cout << "adler32=0x" << std::hex << adler << '\n';

    if (h32 != h32_utils || h32 != stream.final())
    {
        std::cerr << "facade mismatch\n";
        return 2;
    }

    return 0;
}
