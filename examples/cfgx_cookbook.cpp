#include "cfgx.h"

#include <cstdint>
#include <iostream>
#include <optional>
#include <vector>

namespace
{

cfgx::Node Object(std::initializer_list<std::pair<std::string, cfgx::Node>> fields)
{
    cfgx::Node::Object object;
    for (const auto& field : fields)
    {
        object.push_back(field);
    }
    return cfgx::Node(std::move(object));
}

} // namespace

int main()
{
    // Scenario 1: path writes create nested objects and keep call sites compact.
    cfgx::Node root = cfgx::Node::MakeObject();
    cfgx::SetNode(root, "svc.host", cfgx::Node("127.0.0.1"));
    cfgx::SetNode(root, "svc.port", cfgx::Node(std::int64_t(8080)));
    std::cout << "port=" << cfgx::GetNode(root, "svc.port").value->AsInt() << "\n";

    // Scenario 2: JSON parse and merge are explicit about overlay behavior.
    auto overlay = cfgx::ParseJson(R"({"svc":{"port":9090},"features":["a"]})");
    if (overlay.ok)
    {
        cfgx::Merge(root, overlay.value, false);
    }
    std::cout << "merged=" << cfgx::ToJson(root, 0) << "\n";

    // Scenario 3: validation returns all issues unless a rule asks for fail-fast.
    const auto validation =
        cfgx::Validate(root, {cfgx::RequirePathRule("svc.host"), cfgx::NumericRangeRule("svc.port", 1, 65535),
                              cfgx::ChoiceRule("mode", {"dev", "prod"})});
    std::cout << "validation-ok=" << validation.ok << " issues=" << validation.value.size() << "\n";

    // Scenario 4: env/local/runtime layers compose with source attribution.
    const auto env_layer = cfgx::BuildEnvLayerFromPairs({{"APP_CFG_SVC__PORT", "7070"}}, "APP_CFG_");
    cfgx::RuntimeOverrides runtime;
    runtime.Set("svc.host", cfgx::Node("localhost"));
    std::vector<cfgx::SourceAttribution> trace;
    const auto composed = cfgx::ComposeLayers(root, env_layer.value, std::nullopt, &runtime, {}, &trace);
    std::cout << "composed-ok=" << composed.ok << " trace=" << trace.size() << "\n";

    // Scenario 5: RuntimeOverrides can also materialize its own patch document.
    runtime.Set("feature.enabled", cfgx::Node(true));
    const auto patch = runtime.Materialize();
    std::cout << "patch-kind=" << cfgx::ToString(patch.value.Kind()) << "\n";
    (void)Object;
    return 0;
}
