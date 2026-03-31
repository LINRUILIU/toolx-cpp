#include <memory>

#include "logsys.h"

int main()
{
    using namespace logsys;

    DefaultLoggerOptions options;
    options.level = LogLevel::Debug;
    options.enable_console = true;
    options.enable_file = true;
    options.file_path = "app.log";
    options.use_json_formatter = false;

    auto &logger = Logger::Instance();
    logger.ConfigureDefaultLogger(options);
    logger.SetDefaultOrigin(ErrorSource::Business, ModuleId::BusinessCommon, ErrorCategory::Business);

    LOGI("startup mode=%s", "default");

    LOGE_STREAM()
            .SetField("module", "example")
            .SetField("request_id", "req-001")
        << "stream message id=" << 42;

    return 0;
}
