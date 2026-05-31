#include "resultx.h"

#include <iostream>

int main()
{
    // Scenario 1: cfgx status can be normalized into the shared sysx shape.
    const auto cfg_status = cfgx::Status{false, "missing key"};
    const auto normalized = resultx::FromCfgx(cfg_status, resultx::ErrorKind::NotFound);
    std::cout << "cfgx=" << resultx::FormatError(normalized.error) << "\n";

    // Scenario 2: asyncx cancellation/queue errors keep retryability metadata.
    asyncx::Status async_status;
    async_status.ok = false;
    async_status.error = {asyncx::ErrorKind::Cancelled, false, "cancelled by caller"};
    const auto async_normalized = resultx::FromAsyncx(async_status);
    std::cout << "asyncx=" << resultx::FormatError(async_normalized.error) << "\n";

    // Scenario 3: httpx errors preserve network domain and HTTP/native code.
    httpx::Status http_status;
    http_status.ok = false;
    http_status.error = {httpx::ErrorKind::Timeout, 504, true, "gateway timeout"};
    const auto http_normalized = resultx::FromHttpx(http_status);
    std::cout << "httpx=" << resultx::FormatError(http_normalized.error) << "\n";

    // Scenario 4: Propagate converts a Status into a typed Result at API boundaries.
    const resultx::Result<int> propagated = resultx::Propagate<int>(normalized);
    std::cout << "propagated-ok=" << propagated.ok << "\n";
    return 0;
}
