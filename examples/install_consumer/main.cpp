#include "asyncx.h"
#include "cfgx.h"
#include "logsys.h"
#include "schemax.h"

int main()
{
    cfgx::Node root = cfgx::Node::MakeObject();
    if (!cfgx::SetNode(root, "consumer.enabled", cfgx::Node(true)).ok)
    {
        return 2;
    }

    auto& logger = logsys::Logger::Instance();
    logger.ConfigureSimpleLogger(logsys::LogLevel::Fatal, false, false);
    logger.LogDefaultf(logsys::LogLevel::Info, __FILE__, __LINE__, __func__, "install-consumer");
    logger.Flush();

    cfgx::Node schema_root = cfgx::Node::MakeObject();
    if (!schema_root.Set("type", cfgx::Node("object")).ok)
    {
        return 3;
    }

    auto schema = schemax::Compile(schema_root);
    if (!schema.ok)
    {
        return 4;
    }
    if (!schemax::Validate(root, schema.value).empty())
    {
        return 5;
    }

    asyncx::CancellationSource cancellation;
    cancellation.RequestCancel();
    if (!cancellation.Token().IsCancellationRequested())
    {
        return 6;
    }

    return cfgx::Exists(root, "consumer.enabled") ? 0 : 1;
}
