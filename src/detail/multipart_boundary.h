#pragma once
#include "httpx.h"
#include <algorithm>
#include <string>

namespace toolx_detail
{
// Bound the work even if input contains every generated candidate or entropy fails.
// The generator is private and injectable so collision/exhaustion tests are deterministic.
template <typename Generator>
bool SelectMultipartBoundary(const std::vector<httpx::MultipartPart>& parts, Generator generate, std::string* boundary)
{
    constexpr int max_attempts = 8;
    for (int attempt = 0; attempt < max_attempts; ++attempt)
    {
        auto candidate = generate();
        if (!candidate.empty() && !std::any_of(parts.begin(), parts.end(), [&](const auto& part)
                                               { return part.data.find(candidate) != std::string::npos; }))
        {
            *boundary = std::move(candidate);
            return true;
        }
    }
    boundary->clear();
    return false;
}
} // namespace toolx_detail
