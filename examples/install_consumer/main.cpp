#include "cfgx.h"
#include "logsys.h"

int main()
{
    cfgx::Node root = cfgx::Node::MakeObject();
    if (!cfgx::SetNode(root, "consumer.enabled", cfgx::Node(true)).ok)
    {
        return 2;
    }

    auto &logger = logsys::Logger::Instance();
    logger.ConfigureSimpleLogger(logsys::LogLevel::Fatal, false, false);
    logger.LogDefaultf(logsys::LogLevel::Info, __FILE__, __LINE__, __func__, "install-consumer");
    logger.Flush();

    return cfgx::Exists(root, "consumer.enabled") ? 0 : 1;
}
