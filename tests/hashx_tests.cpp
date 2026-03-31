#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "hashx.h"
#include "utils.h"

TEST(HashxTests, Fnv1a32KnownVectors)
{
    EXPECT_EQ(hashx::fnv1a32(""), 0x811C9DC5u);
    EXPECT_EQ(hashx::fnv1a32("a"), 0xE40C292Cu);
    EXPECT_EQ(hashx::fnv1a32("hello"), 0x4F9F2CABu);
}

TEST(HashxTests, Fnv1a64KnownVectors)
{
    EXPECT_EQ(hashx::fnv1a64(""), 0xCBF29CE484222325ull);
    EXPECT_EQ(hashx::fnv1a64("a"), 0xAF63DC4C8601EC8Cull);
    EXPECT_EQ(hashx::fnv1a64("hello"), 0xA430D84680AABD0Bull);
}

TEST(HashxTests, Crc32KnownVectors)
{
    EXPECT_EQ(hashx::crc32(""), 0x00000000u);
    EXPECT_EQ(hashx::crc32("123456789"), 0xCBF43926u);
    EXPECT_EQ(hashx::crc32("hello"), 0x3610A686u);
}

TEST(HashxTests, BytesAndTextApisMatch)
{
    constexpr std::string_view text = "hash-bytes";
    EXPECT_EQ(hashx::fnv1a32(text), hashx::fnv1a32_bytes(text.data(), text.size()));
    EXPECT_EQ(hashx::fnv1a64(text), hashx::fnv1a64_bytes(text.data(), text.size()));
    EXPECT_EQ(hashx::crc32(text), hashx::crc32_bytes(text.data(), text.size()));
}

TEST(HashxTests, BinaryInputSupported)
{
    const std::array<unsigned char, 5> bytes{{0x00u, 0xFFu, 0x10u, 0x00u, 0x7Fu}};
    const std::uint32_t h32 = hashx::fnv1a32_bytes(bytes.data(), bytes.size());
    const std::uint64_t h64 = hashx::fnv1a64_bytes(bytes.data(), bytes.size());
    const std::uint32_t crc = hashx::crc32_bytes(bytes.data(), bytes.size());

    EXPECT_NE(h32, 0u);
    EXPECT_NE(h64, 0ull);
    EXPECT_NE(crc, 0u);
}

TEST(HashxTests, StreamingStatesMatchOneShot)
{
    constexpr std::string_view text = "streaming-hash-check";

    hashx::Fnv1a32State fnv32;
    fnv32.update(text.data(), 5);
    fnv32.update(text.data() + 5, text.size() - 5);
    EXPECT_EQ(fnv32.final(), hashx::fnv1a32(text));

    hashx::Fnv1a64State fnv64;
    fnv64.update(text.data(), 7);
    fnv64.update(text.data() + 7, text.size() - 7);
    EXPECT_EQ(fnv64.final(), hashx::fnv1a64(text));

    hashx::Crc32State crc;
    crc.update(text.data(), 3);
    crc.update(text.data() + 3, text.size() - 3);
    EXPECT_EQ(crc.final(), hashx::crc32(text));
}

TEST(HashxTests, Adler32KnownVectors)
{
    EXPECT_EQ(hashx::adler32(""), 0x00000001u);
    EXPECT_EQ(hashx::adler32("123456789"), 0x091E01DEu);

    hashx::Adler32State st;
    constexpr std::string_view text = "adler-stream";
    st.update(text.data(), 5);
    st.update(text.data() + 5, text.size() - 5);
    EXPECT_EQ(st.final(), hashx::adler32(text));
}

TEST(HashxTests, UtilsHashFacadeConsistency)
{
    constexpr std::string_view text = "hash-compat-check";

    EXPECT_EQ(hashx::fnv1a32(text), utils::hash::fnv1a32(text));
    EXPECT_EQ(hashx::fnv1a64(text), utils::hash::fnv1a64(text));
    EXPECT_EQ(hashx::crc32(text), utils::hash::crc32(text));
    EXPECT_EQ(hashx::adler32(text), utils::hash::adler32(text));

    hashx::Fnv1a32State s1;
    utils::hash::Fnv1a32State s2;
    s1.update(text.data(), text.size());
    s2.update(text.data(), text.size());
    EXPECT_EQ(s1.final(), s2.final());
}
