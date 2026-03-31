#include <iostream>
#include <vector>

#include "cfgx.h"

int main()
{
    // Parse from JSON, then mutate/merge/validate through unified cfgx APIs.
    const auto parsed = cfgx::ParseJson(R"({"service":{"name":"alpha","port":8080},"features":{"tls":false}})");
    if (!parsed.ok)
    {
        std::cerr << "parse failed: " << parsed.error << "\n";
        return 1;
    }

    cfgx::Node root = parsed.value;

    auto set_st = cfgx::SetNode(root, "features.tls", cfgx::Node(true));
    if (!set_st.ok)
    {
        std::cerr << "set failed: " << set_st.error << "\n";
        return 1;
    }

    cfgx::Node overlay = cfgx::Node::MakeObject();
    cfgx::SetNode(overlay, "service.timeout_ms", cfgx::Node(std::int64_t(1500)));

    auto merge_st = cfgx::Merge(root, overlay);
    if (!merge_st.ok)
    {
        std::cerr << "merge failed: " << merge_st.error << "\n";
        return 1;
    }

    std::vector<cfgx::ValidationRule> rules;
    rules.push_back(cfgx::RequirePathRule("service.name"));
    rules.push_back(cfgx::ExpectKindRule("service.port", cfgx::NodeKind::Integer));
    rules.push_back(cfgx::NumericRangeRule("service.port", 1, 65535));

    const auto validation = cfgx::Validate(root, rules);
    if (!validation.ok)
    {
        std::cerr << "validation issues=" << validation.value.size() << "\n";
        for (const auto &issue : validation.value)
        {
            std::cerr << "- path=" << issue.path << " message=" << issue.message << "\n";
        }
        return 1;
    }

    std::cout << cfgx::ToJson(root, 2) << "\n";
    return 0;
}
