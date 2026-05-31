#include "textcodec.h"

#include <array>
#include <iostream>

int main()
{
    // Scenario 1: hex encode/decode is explicit about invalid input.
    const auto hex = textcodec::hex_encode("ToolX", true);
    const auto decoded_hex = textcodec::hex_decode(hex);
    std::cout << "hex=" << hex << " ok=" << decoded_hex.ok << "\n";

    // Scenario 2: buffer decoding reports required capacity failures cleanly.
    std::array<char, 2> small{};
    const auto buffer_result = textcodec::hex_decode_to_buffer("546f6f6c58", small.data(), small.size());
    std::cout << "buffer-ok=" << buffer_result.ok << " code=" << textcodec::ToString(buffer_result.code) << "\n";

    // Scenario 3: Base64 URL-safe mode and padding are controlled by options.
    textcodec::Base64Options b64;
    b64.variant = textcodec::Base64Variant::UrlSafe;
    b64.padding = false;
    const auto encoded = textcodec::base64_encode("a/b?", b64);
    std::cout << "b64-url=" << encoded << " roundtrip=" << textcodec::base64_decode(encoded, b64).value << "\n";

    // Scenario 4: URL plus/space behavior is explicit for form-style data.
    textcodec::UrlEncodeOptions enc;
    enc.space_policy = textcodec::UrlSpacePolicy::Plus;
    textcodec::UrlDecodeOptions dec;
    dec.plus_policy = textcodec::UrlDecodePlusPolicy::PlusAsSpace;
    const auto url = textcodec::url_encode("hello world", enc);
    std::cout << "url=" << url << " decoded=" << textcodec::url_decode(url, dec).value << "\n";
    return 0;
}
