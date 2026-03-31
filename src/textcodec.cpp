#include "textcodec.h"

#include "utils.h"

#include <array>
#include <cstdint>
#include <cstring>

namespace textcodec
{
    namespace
    {

        DecodeResult<std::string> make_error(DecodeError code,
                                             std::string_view key,
                                             std::string_view reason)
        {
            DecodeResult<std::string> out;
            out.code = code;
            out.error = utils::err::join_context("textcodec", key, reason);
            return out;
        }

        bool is_unreserved(unsigned char c)
        {
            if (c >= 'A' && c <= 'Z')
            {
                return true;
            }
            if (c >= 'a' && c <= 'z')
            {
                return true;
            }
            if (c >= '0' && c <= '9')
            {
                return true;
            }
            return c == '-' || c == '_' || c == '.' || c == '~';
        }

        int hex_value(char c)
        {
            if (c >= '0' && c <= '9')
            {
                return c - '0';
            }
            if (c >= 'a' && c <= 'f')
            {
                return 10 + (c - 'a');
            }
            if (c >= 'A' && c <= 'F')
            {
                return 10 + (c - 'A');
            }
            return -1;
        }

        char hex_char(unsigned int v, bool uppercase)
        {
            if (v < 10u)
            {
                return static_cast<char>('0' + v);
            }
            return static_cast<char>((uppercase ? 'A' : 'a') + (v - 10u));
        }

        const std::array<int, 256> &base64_decode_table()
        {
            static const std::array<int, 256> table = []
            {
                std::array<int, 256> t{};
                t.fill(-1);

                const std::string chars =
                    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
                for (int i = 0; i < static_cast<int>(chars.size()); ++i)
                {
                    t[static_cast<unsigned char>(chars[static_cast<std::size_t>(i)])] = i;
                }
                return t;
            }();

            return table;
        }

        const std::array<int, 256> &base64url_decode_table()
        {
            static const std::array<int, 256> table = []
            {
                std::array<int, 256> t{};
                t.fill(-1);

                const std::string chars =
                    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
                for (int i = 0; i < static_cast<int>(chars.size()); ++i)
                {
                    t[static_cast<unsigned char>(chars[static_cast<std::size_t>(i)])] = i;
                }
                return t;
            }();

            return table;
        }

        std::string base64_encode_impl(std::string_view input,
                                       const Base64Options &options)
        {
            static constexpr char kTableStd[] =
                "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            static constexpr char kTableUrl[] =
                "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

            const char *table = (options.variant == Base64Variant::UrlSafe) ? kTableUrl : kTableStd;

            std::string out;
            out.reserve(((input.size() + 2u) / 3u) * 4u);

            std::size_t i = 0;
            while (i + 3u <= input.size())
            {
                const std::uint32_t b0 = static_cast<unsigned char>(input[i]);
                const std::uint32_t b1 = static_cast<unsigned char>(input[i + 1u]);
                const std::uint32_t b2 = static_cast<unsigned char>(input[i + 2u]);
                const std::uint32_t x = (b0 << 16u) | (b1 << 8u) | b2;

                out.push_back(table[(x >> 18u) & 0x3Fu]);
                out.push_back(table[(x >> 12u) & 0x3Fu]);
                out.push_back(table[(x >> 6u) & 0x3Fu]);
                out.push_back(table[x & 0x3Fu]);

                i += 3u;
            }

            const std::size_t rem = input.size() - i;
            if (rem == 1u)
            {
                const std::uint32_t b0 = static_cast<unsigned char>(input[i]);
                const std::uint32_t x = (b0 << 16u);
                out.push_back(table[(x >> 18u) & 0x3Fu]);
                out.push_back(table[(x >> 12u) & 0x3Fu]);
                if (options.padding)
                {
                    out.push_back('=');
                    out.push_back('=');
                }
            }
            else if (rem == 2u)
            {
                const std::uint32_t b0 = static_cast<unsigned char>(input[i]);
                const std::uint32_t b1 = static_cast<unsigned char>(input[i + 1u]);
                const std::uint32_t x = (b0 << 16u) | (b1 << 8u);
                out.push_back(table[(x >> 18u) & 0x3Fu]);
                out.push_back(table[(x >> 12u) & 0x3Fu]);
                out.push_back(table[(x >> 6u) & 0x3Fu]);
                if (options.padding)
                {
                    out.push_back('=');
                }
            }

            return out;
        }

        DecodeResult<std::string> base64_decode_impl(std::string_view input,
                                                     const Base64Options &options)
        {
            std::string normalized(input);
            if (!options.padding)
            {
                const std::size_t mod = normalized.size() % 4u;
                if (mod == 1u)
                {
                    return make_error(DecodeError::InvalidPadding,
                                      "base64_decode",
                                      "invalid base64 length for no-padding mode");
                }
                if (mod != 0u)
                {
                    normalized.append(4u - mod, '=');
                }
            }
            else if ((normalized.size() % 4u) != 0u)
            {
                return make_error(DecodeError::InvalidPadding,
                                  "base64_decode",
                                  "base64 length must be a multiple of 4");
            }

            const auto &table = (options.variant == Base64Variant::UrlSafe)
                                    ? base64url_decode_table()
                                    : base64_decode_table();

            DecodeResult<std::string> out;
            out.value.reserve((normalized.size() / 4u) * 3u);

            for (std::size_t i = 0; i < normalized.size(); i += 4u)
            {
                const char c0 = normalized[i];
                const char c1 = normalized[i + 1u];
                const char c2 = normalized[i + 2u];
                const char c3 = normalized[i + 3u];

                if (c0 == '=' || c1 == '=')
                {
                    return make_error(DecodeError::InvalidPadding,
                                      "base64_decode",
                                      "invalid padding position");
                }

                const int v0 = table[static_cast<unsigned char>(c0)];
                const int v1 = table[static_cast<unsigned char>(c1)];
                if (v0 < 0 || v1 < 0)
                {
                    return make_error(DecodeError::InvalidCharacter,
                                      "base64_decode",
                                      "invalid base64 character");
                }

                const bool pad2 = (c2 == '=');
                const bool pad3 = (c3 == '=');

                if (pad2)
                {
                    if (!pad3 || i + 4u != normalized.size())
                    {
                        return make_error(DecodeError::InvalidPadding,
                                          "base64_decode",
                                          "invalid final block padding");
                    }

                    const std::uint32_t x = (static_cast<std::uint32_t>(v0) << 18u) |
                                            (static_cast<std::uint32_t>(v1) << 12u);
                    out.value.push_back(static_cast<char>((x >> 16u) & 0xFFu));
                    continue;
                }

                const int v2 = table[static_cast<unsigned char>(c2)];
                if (v2 < 0)
                {
                    return make_error(DecodeError::InvalidCharacter,
                                      "base64_decode",
                                      "invalid base64 character");
                }

                if (pad3)
                {
                    if (i + 4u != normalized.size())
                    {
                        return make_error(DecodeError::InvalidPadding,
                                          "base64_decode",
                                          "padding only allowed in final block");
                    }

                    const std::uint32_t x = (static_cast<std::uint32_t>(v0) << 18u) |
                                            (static_cast<std::uint32_t>(v1) << 12u) |
                                            (static_cast<std::uint32_t>(v2) << 6u);
                    out.value.push_back(static_cast<char>((x >> 16u) & 0xFFu));
                    out.value.push_back(static_cast<char>((x >> 8u) & 0xFFu));
                    continue;
                }

                const int v3 = table[static_cast<unsigned char>(c3)];
                if (v3 < 0)
                {
                    return make_error(DecodeError::InvalidCharacter,
                                      "base64_decode",
                                      "invalid base64 character");
                }

                const std::uint32_t x = (static_cast<std::uint32_t>(v0) << 18u) |
                                        (static_cast<std::uint32_t>(v1) << 12u) |
                                        (static_cast<std::uint32_t>(v2) << 6u) |
                                        static_cast<std::uint32_t>(v3);
                out.value.push_back(static_cast<char>((x >> 16u) & 0xFFu));
                out.value.push_back(static_cast<char>((x >> 8u) & 0xFFu));
                out.value.push_back(static_cast<char>(x & 0xFFu));
            }

            out.ok = true;
            out.code = DecodeError::None;
            return out;
        }

    } // namespace

    std::string hex_encode_bytes(const void *data, std::size_t size, bool uppercase)
    {
        return hex_encode(std::string_view(static_cast<const char *>(data), size), uppercase);
    }

    std::string hex_encode(std::string_view input, bool uppercase)
    {
        std::string out;
        out.resize(input.size() * 2u);

        for (std::size_t i = 0; i < input.size(); ++i)
        {
            const auto b = static_cast<unsigned char>(input[i]);
            out[i * 2u] = hex_char((b >> 4u) & 0x0Fu, uppercase);
            out[i * 2u + 1u] = hex_char(b & 0x0Fu, uppercase);
        }

        return out;
    }

    DecodeResult<std::string> hex_decode(std::string_view input)
    {
        if ((input.size() % 2u) != 0u)
        {
            return make_error(DecodeError::TruncatedInput,
                              "hex_decode",
                              "hex input length must be even");
        }

        DecodeResult<std::string> out;
        out.value.resize(input.size() / 2u);

        for (std::size_t i = 0; i < input.size(); i += 2u)
        {
            const int hi = hex_value(input[i]);
            const int lo = hex_value(input[i + 1u]);
            if (hi < 0 || lo < 0)
            {
                return make_error(DecodeError::InvalidCharacter,
                                  "hex_decode",
                                  "invalid hex character");
            }
            out.value[i / 2u] = static_cast<char>((hi << 4) | lo);
        }

        out.ok = true;
        out.code = DecodeError::None;
        return out;
    }

    DecodeResult<std::size_t> hex_decode_to_buffer(std::string_view input,
                                                   void *out_buffer,
                                                   std::size_t out_capacity)
    {
        DecodeResult<std::size_t> out;
        const auto decoded = hex_decode(input);
        if (!decoded.ok)
        {
            out.code = decoded.code;
            out.error = decoded.error;
            return out;
        }

        if (decoded.value.size() > out_capacity)
        {
            out.code = DecodeError::OutOfRange;
            out.error = utils::err::join_context("textcodec", "hex_decode_to_buffer", "output buffer too small");
            return out;
        }

        if (!decoded.value.empty())
        {
            std::memcpy(out_buffer, decoded.value.data(), decoded.value.size());
        }
        out.ok = true;
        out.code = DecodeError::None;
        out.value = decoded.value.size();
        return out;
    }

    std::string base64_encode(std::string_view input)
    {
        Base64Options options;
        return base64_encode_impl(input, options);
    }

    std::string base64_encode(std::string_view input, const Base64Options &options)
    {
        return base64_encode_impl(input, options);
    }

    DecodeResult<std::string> base64_decode(std::string_view input)
    {
        Base64Options options;
        return base64_decode_impl(input, options);
    }

    DecodeResult<std::string> base64_decode(std::string_view input, const Base64Options &options)
    {
        return base64_decode_impl(input, options);
    }

    std::string url_encode(std::string_view input)
    {
        UrlEncodeOptions options;
        return url_encode(input, options);
    }

    std::string url_encode(std::string_view input, const UrlEncodeOptions &options)
    {
        std::string out;
        out.reserve(input.size() * 3u);

        for (unsigned char c : input)
        {
            if (c == ' ' && options.space_policy == UrlSpacePolicy::Plus)
            {
                out.push_back('+');
                continue;
            }
            if (is_unreserved(c))
            {
                out.push_back(static_cast<char>(c));
            }
            else
            {
                out.push_back('%');
                out.push_back(hex_char((c >> 4u) & 0x0Fu, true));
                out.push_back(hex_char(c & 0x0Fu, true));
            }
        }

        return out;
    }

    DecodeResult<std::string> url_decode(std::string_view input)
    {
        UrlDecodeOptions options;
        return url_decode(input, options);
    }

    DecodeResult<std::string> url_decode(std::string_view input, const UrlDecodeOptions &options)
    {
        DecodeResult<std::string> out;
        out.value.reserve(input.size());

        for (std::size_t i = 0; i < input.size(); ++i)
        {
            const char c = input[i];
            if (c != '%')
            {
                if (c == '+' && options.plus_policy == UrlDecodePlusPolicy::PlusAsSpace)
                {
                    out.value.push_back(' ');
                }
                else
                {
                    out.value.push_back(c);
                }
                continue;
            }

            if (i + 2u >= input.size())
            {
                return make_error(DecodeError::TruncatedInput,
                                  "url_decode",
                                  "truncated percent-encoding sequence");
            }

            const int hi = hex_value(input[i + 1u]);
            const int lo = hex_value(input[i + 2u]);
            if (hi < 0 || lo < 0)
            {
                return make_error(DecodeError::InvalidPercentEncoding,
                                  "url_decode",
                                  "invalid percent-encoding sequence");
            }

            out.value.push_back(static_cast<char>((hi << 4) | lo));
            i += 2u;
        }

        out.ok = true;
        out.code = DecodeError::None;
        return out;
    }

    const char *ToString(DecodeError code) noexcept
    {
        switch (code)
        {
        case DecodeError::None:
            return "None";
        case DecodeError::InvalidCharacter:
            return "InvalidCharacter";
        case DecodeError::InvalidPadding:
            return "InvalidPadding";
        case DecodeError::TruncatedInput:
            return "TruncatedInput";
        case DecodeError::InvalidPercentEncoding:
            return "InvalidPercentEncoding";
        case DecodeError::PolicyConflict:
            return "PolicyConflict";
        case DecodeError::OutOfRange:
            return "OutOfRange";
        }
        return "Unknown";
    }

} // namespace textcodec
