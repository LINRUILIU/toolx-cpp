#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace textcodec
{

    enum class DecodeError
    {
        None = 0,
        InvalidCharacter,
        InvalidPadding,
        TruncatedInput,
        InvalidPercentEncoding,
        PolicyConflict,
        OutOfRange
    };

    enum class Base64Variant
    {
        Standard,
        UrlSafe
    };

    enum class UrlSpacePolicy
    {
        Percent20,
        Plus
    };

    enum class UrlDecodePlusPolicy
    {
        PreservePlus,
        PlusAsSpace
    };

    struct Base64Options
    {
        Base64Variant variant{Base64Variant::Standard};
        bool padding{true};
    };

    struct UrlEncodeOptions
    {
        UrlSpacePolicy space_policy{UrlSpacePolicy::Percent20};
    };

    struct UrlDecodeOptions
    {
        UrlDecodePlusPolicy plus_policy{UrlDecodePlusPolicy::PreservePlus};
    };

    template <typename T>
    struct DecodeResult
    {
        bool ok{false};
        T value{};
        DecodeError code{DecodeError::None};
        std::string error;
    };

    std::string hex_encode(std::string_view input, bool uppercase = false);// 十六进制编码方法，参数为输入字符串和是否使用大写字母，返回十六进制编码后的字符串。
    std::string hex_encode_bytes(const void *data, std::size_t size, bool uppercase = false);// 十六进制编码方法，参数为输入字节数据、数据大小以及是否使用大写字母，返回十六进制编码后的字符串。
    DecodeResult<std::string> hex_decode(std::string_view input);// 十六进制解码方法，参数为输入十六进制字符串，返回解码结果，包括是否成功、解码后的字符串、错误代码以及错误信息等字段。
    DecodeResult<std::size_t> hex_decode_to_buffer(std::string_view input,
                                                   void *out_buffer,
                                                   std::size_t out_capacity);// 十六进制解码到缓冲区方法，参数为输入十六进制字符串、输出缓冲区、缓冲区容量，返回解码结果，包括是否成功、解码后的字节数、错误代码以及错误信息等字段。

    std::string base64_encode(std::string_view input);// Base64编码方法，参数为输入字符串，返回Base64编码后的字符串。
    std::string base64_encode(std::string_view input, const Base64Options &options);// Base64编码方法，参数为输入字符串和编码选项，返回Base64编码后的字符串。
    DecodeResult<std::string> base64_decode(std::string_view input);// Base64解码方法，参数为输入Base64字符串，返回解码结果，包括是否成功、解码后的字符串、错误代码以及错误信息等字段。
    DecodeResult<std::string> base64_decode(std::string_view input, const Base64Options &options);// Base64解码方法，参数为输入Base64字符串和编码选项，返回解码结果，包括是否成功、解码后的字符串、错误代码以及错误信息等字段。

    std::string url_encode(std::string_view input);// URL编码方法，参数为输入字符串，返回URL编码后的字符串。
    std::string url_encode(std::string_view input, const UrlEncodeOptions &options);// URL编码方法，参数为输入字符串和编码选项，返回URL编码后的字符串。
    DecodeResult<std::string> url_decode(std::string_view input);// URL解码方法，参数为输入URL字符串，返回解码结果，包括是否成功、解码后的字符串、错误代码以及错误信息等字段。
    DecodeResult<std::string> url_decode(std::string_view input, const UrlDecodeOptions &options);// URL解码方法，参数为输入URL字符串和解码选项，返回解码结果，包括是否成功、解码后的字符串、错误代码以及错误信息等字段。

    const char *ToString(DecodeError code) noexcept;

} // namespace textcodec
