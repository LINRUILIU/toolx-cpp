#include <iostream>
#include <vector>

#include "textcodec.h"

int main()
{
    const std::string data = "A b+/";

    const auto h = textcodec::hex_encode(data);
    const auto b = textcodec::base64_encode(data);
    const auto u = textcodec::url_encode(data);

    std::cout << "hex=" << h << '\n';
    std::cout << "b64=" << b << '\n';
    std::cout << "url=" << u << '\n';

    textcodec::Base64Options b64_opt;
    b64_opt.variant = textcodec::Base64Variant::UrlSafe;
    b64_opt.padding = false;
    const auto b64_url = textcodec::base64_encode(data, b64_opt);
    std::cout << "b64_url=" << b64_url << '\n';

    textcodec::UrlEncodeOptions url_opt;
    url_opt.space_policy = textcodec::UrlSpacePolicy::Plus;
    const auto url_plus = textcodec::url_encode(data, url_opt);
    std::cout << "url_plus=" << url_plus << '\n';

    textcodec::UrlDecodeOptions decode_opt;
    decode_opt.plus_policy = textcodec::UrlDecodePlusPolicy::PlusAsSpace;
    const auto url_decoded = textcodec::url_decode(url_plus, decode_opt);
    if (!url_decoded.ok)
    {
        std::cout << "url decode failed: " << url_decoded.error << '\n';
        return 2;
    }

    std::vector<unsigned char> buf(32, 0);
    const auto hex_into_buf = textcodec::hex_decode_to_buffer(h, buf.data(), buf.size());
    if (!hex_into_buf.ok)
    {
        std::cout << "hex decode failed: " << hex_into_buf.error << '\n';
        return 2;
    }

    const auto decoded = textcodec::base64_decode(b);
    if (!decoded.ok)
    {
        std::cout << decoded.error << '\n';
        return 2;
    }

    std::cout << "decoded=" << decoded.value << '\n';
    std::cout << "url_decoded=" << url_decoded.value << '\n';
    std::cout << "hex_bytes=" << hex_into_buf.value << '\n';
    return 0;
}
