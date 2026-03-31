#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "utils.h"

namespace hashx
{
    using Fnv1a32State = utils::hash::Fnv1a32State;
    using Fnv1a64State = utils::hash::Fnv1a64State;
    using Crc32State = utils::hash::Crc32State;
    using Adler32State = utils::hash::Adler32State;

    std::uint32_t fnv1a32_bytes(const void *data, std::size_t size) noexcept; // 计算FNV-1a 32-bit哈希值的方法，参数为输入数据的指针和大小，返回计算得到的哈希值。
    std::uint64_t fnv1a64_bytes(const void *data, std::size_t size) noexcept; // 计算FNV-1a 64-bit哈希值的方法，参数为输入数据的指针和大小，返回计算得到的哈希值。
    std::uint32_t crc32_bytes(const void *data, std::size_t size) noexcept;   // 计算CRC32哈希值的方法，参数为输入数据的指针和大小，返回计算得到的哈希值。
    std::uint32_t adler32_bytes(const void *data, std::size_t size) noexcept; // 计算Adler-32哈希值的方法，参数为输入数据的指针和大小，返回计算得到的哈希值。

    std::uint32_t fnv1a32(std::string_view text) noexcept; // 计算FNV-1a 32-bit哈希值的方法，参数为输入文本，返回计算得到的哈希值。
    std::uint64_t fnv1a64(std::string_view text) noexcept; // 计算FNV-1a 64-bit哈希值的方法，参数为输入文本，返回计算得到的哈希值。
    std::uint32_t crc32(std::string_view text) noexcept;   // 计算CRC32哈希值的方法，参数为输入文本，返回计算得到的哈希值。
    std::uint32_t adler32(std::string_view text) noexcept; // 计算Adler-32哈希值的方法，参数为输入文本，返回计算得到的哈希值。

} // namespace hashx
