#include <filesystem>
#include <iostream>

#include "cfgx.h"
#include "logsys.h"

int main()
{
    namespace fs = std::filesystem;

    const fs::path root = "temp/logsys_cfgx";
    const fs::path config_file = root / "logger.json";
    std::error_code ec;
    fs::create_directories(root, ec);

    cfgx::Node config = cfgx::Node::MakeObject();
    cfgx::SetNode(config, "global_record_level", cfgx::Node("debug"));
    cfgx::SetNode(config, "global_output_level", cfgx::Node("info"));
    cfgx::SetNode(config, "global_enable_console", cfgx::Node(true));
    cfgx::SetNode(config, "global_enable_file", cfgx::Node(false));
    cfgx::SetNode(config, "global_enable_debugger", cfgx::Node(false));
    cfgx::SetNode(config, "global_text_field_mask", cfgx::Node(std::int64_t(logsys::kTextFieldMaskDefault)));

    const auto saved = cfgx::SaveToFile(config, config_file.string());
    if (!saved.ok)
    {
        std::cerr << "save failed: " << saved.error << '\n';
        return 2;
    }

    auto &logger = logsys::Logger::Instance();
    if (!logger.LoadConfigV2FromJsonFile(config_file.string()))
    {
        std::cerr << "logger config load failed\n";
        return 2;
    }

    logger.SetDefaultOrigin(logsys::ErrorSource::Business,
                            logsys::ModuleId::BusinessCommon,
                            logsys::ErrorCategory::Business);
    LOGI("logsys configured through cfgx-authored json");
    logger.Flush();

    std::cout << "record_level=" << static_cast<int>(logger.RecordLevel()) << '\n';
    std::cout << "output_level=" << static_cast<int>(logger.Level()) << '\n';
    return 0;
}
