#include "logsys.h"

#include <iostream>

int main()
{
    auto& logger = logsys::Logger::Instance();

    // Scenario 1: simple logger setup is enough for small tools.
    logger.ConfigureSimpleLogger(logsys::LogLevel::Info, true, false);
    logger.ResetMetrics();
    LOGI("hello %s", "toolx");

    // Scenario 2: structured context fields are inherited by logs in the scope.
    logsys::LogContext context;
    context.SetField("request", "abc123").SetField("component", "cookbook");
    {
        logsys::ScopedLogContext scoped(context);
        LOGW("context fields are attached");
    }

    // Scenario 3: TraceSpan records duration on destruction.
    {
        logsys::TraceSpan span("work");
        span.SetField("phase", "example");
    }

    // Scenario 4: metrics snapshots are cheap operational counters.
    logger.Flush();
    const auto metrics = logger.GetMetricsSnapshot();
    std::cout << "accepted=" << metrics.accepted << " emitted=" << metrics.emitted << "\n";

    // Scenario 5: V2 config keeps routing and formatting explicit.
    logsys::LoggerConfigV2 config;
    config.global_output_level = logsys::LogLevel::Error;
    config.global_record_level = logsys::LogLevel::Info;
    config.global_text_field_mask = logsys::kTextFieldMaskSimple;
    logger.ApplyConfigV2(config);
    LOGE("error still emits");
    logger.Flush();
    return 0;
}
