#include "hashx.h"

namespace hashx
{
    std::uint32_t fnv1a32_bytes(const void *data, std::size_t size) noexcept
    {
        return utils::hash::fnv1a32_bytes(data, size);
    }

    std::uint64_t fnv1a64_bytes(const void *data, std::size_t size) noexcept
    {
        return utils::hash::fnv1a64_bytes(data, size);
    }

    std::uint32_t crc32_bytes(const void *data, std::size_t size) noexcept
    {
        return utils::hash::crc32_bytes(data, size);
    }

    std::uint32_t adler32_bytes(const void *data, std::size_t size) noexcept
    {
        return utils::hash::adler32_bytes(data, size);
    }

    std::uint32_t fnv1a32(std::string_view text) noexcept
    {
        return utils::hash::fnv1a32(text);
    }

    std::uint64_t fnv1a64(std::string_view text) noexcept
    {
        return utils::hash::fnv1a64(text);
    }

    std::uint32_t crc32(std::string_view text) noexcept
    {
        return utils::hash::crc32(text);
    }

    std::uint32_t adler32(std::string_view text) noexcept
    {
        return utils::hash::adler32(text);
    }

} // namespace hashx
