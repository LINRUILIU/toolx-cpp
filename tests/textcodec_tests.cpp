#include <gtest/gtest.h>

#include <array>
#include <string>

#include "textcodec.h"

TEST(TextcodecHexTests, EncodeDecodeRoundTrip)
{
    const std::string src = "hello";
    const auto encoded = textcodec::hex_encode(src);
    EXPECT_EQ(encoded, "68656c6c6f");

    const auto decoded = textcodec::hex_decode(encoded);
    ASSERT_TRUE(decoded.ok);
    EXPECT_EQ(decoded.value, src);
    EXPECT_EQ(decoded.code, textcodec::DecodeError::None);
}

TEST(TextcodecHexTests, UppercaseAndErrors)
{
    EXPECT_EQ(textcodec::hex_encode("\xFF", true), "FF");

    const auto bad_char = textcodec::hex_decode("6g");
    ASSERT_FALSE(bad_char.ok);
    EXPECT_EQ(bad_char.code, textcodec::DecodeError::InvalidCharacter);

    const auto odd = textcodec::hex_decode("ABC");
    ASSERT_FALSE(odd.ok);
    EXPECT_EQ(odd.code, textcodec::DecodeError::TruncatedInput);

    const std::array<unsigned char, 3> bytes{{0x00u, 0x11u, 0xFEu}};
    EXPECT_EQ(textcodec::hex_encode_bytes(bytes.data(), bytes.size(), true), "0011FE");

    std::array<unsigned char, 3> out{{0, 0, 0}};
    const auto wrote = textcodec::hex_decode_to_buffer("0011FE", out.data(), out.size());
    ASSERT_TRUE(wrote.ok);
    EXPECT_EQ(wrote.value, 3u);
    EXPECT_EQ(out[0], 0x00u);
    EXPECT_EQ(out[1], 0x11u);
    EXPECT_EQ(out[2], 0xFEu);
}

TEST(TextcodecBase64Tests, EncodeDecodeRoundTrip)
{
    const std::string src = "hello";
    const auto encoded = textcodec::base64_encode(src);
    EXPECT_EQ(encoded, "aGVsbG8=");

    const auto decoded = textcodec::base64_decode(encoded);
    ASSERT_TRUE(decoded.ok);
    EXPECT_EQ(decoded.value, src);
    EXPECT_EQ(decoded.code, textcodec::DecodeError::None);
}

TEST(TextcodecBase64Tests, KnownVectorAndErrors)
{
    EXPECT_EQ(textcodec::base64_encode("123456789"), "MTIzNDU2Nzg5");

    const auto bad_len = textcodec::base64_decode("abc");
    ASSERT_FALSE(bad_len.ok);
    EXPECT_EQ(bad_len.code, textcodec::DecodeError::InvalidPadding);

    const auto bad_char = textcodec::base64_decode("####");
    ASSERT_FALSE(bad_char.ok);
    EXPECT_EQ(bad_char.code, textcodec::DecodeError::InvalidCharacter);

    const auto bad_pad = textcodec::base64_decode("AA=A");
    ASSERT_FALSE(bad_pad.ok);
    EXPECT_EQ(bad_pad.code, textcodec::DecodeError::InvalidPadding);

    textcodec::Base64Options urlsafe;
    urlsafe.variant = textcodec::Base64Variant::UrlSafe;
    urlsafe.padding = false;

    const auto enc_url = textcodec::base64_encode("??", urlsafe);
    EXPECT_EQ(enc_url, "Pz8");

    const auto dec_url = textcodec::base64_decode(enc_url, urlsafe);
    ASSERT_TRUE(dec_url.ok);
    EXPECT_EQ(dec_url.value, "??");
}

TEST(TextcodecUrlTests, EncodeDecodeRoundTrip)
{
    const std::string src = "A b+/%";
    const auto encoded = textcodec::url_encode(src);
    EXPECT_EQ(encoded, "A%20b%2B%2F%25");

    const auto decoded = textcodec::url_decode(encoded);
    ASSERT_TRUE(decoded.ok);
    EXPECT_EQ(decoded.value, src);
    EXPECT_EQ(decoded.code, textcodec::DecodeError::None);
}

TEST(TextcodecUrlTests, DecodeErrors)
{
    const auto truncated = textcodec::url_decode("abc%");
    ASSERT_FALSE(truncated.ok);
    EXPECT_EQ(truncated.code, textcodec::DecodeError::TruncatedInput);

    const auto invalid = textcodec::url_decode("%G0");
    ASSERT_FALSE(invalid.ok);
    EXPECT_EQ(invalid.code, textcodec::DecodeError::InvalidPercentEncoding);

    textcodec::UrlEncodeOptions enc_opt;
    enc_opt.space_policy = textcodec::UrlSpacePolicy::Plus;
    EXPECT_EQ(textcodec::url_encode("a b", enc_opt), "a+b");

    textcodec::UrlDecodeOptions dec_opt;
    dec_opt.plus_policy = textcodec::UrlDecodePlusPolicy::PlusAsSpace;
    const auto plus_dec = textcodec::url_decode("a+b", dec_opt);
    ASSERT_TRUE(plus_dec.ok);
    EXPECT_EQ(plus_dec.value, "a b");
}

TEST(TextcodecTests, ErrorToString)
{
    EXPECT_STREQ(textcodec::ToString(textcodec::DecodeError::None), "None");
    EXPECT_STREQ(textcodec::ToString(textcodec::DecodeError::InvalidCharacter), "InvalidCharacter");
}
