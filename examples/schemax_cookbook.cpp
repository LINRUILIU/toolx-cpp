#include "schemax.h"

#include <cstdint>
#include <iostream>
#include <utility>

namespace
{

cfgx::Node Obj(std::initializer_list<std::pair<std::string, cfgx::Node>> fields)
{
    cfgx::Node::Object object;
    for (const auto& field : fields)
    {
        object.push_back(field);
    }
    return cfgx::Node(std::move(object));
}

cfgx::Node Arr(std::initializer_list<cfgx::Node> values)
{
    return cfgx::Node(cfgx::Node::Array(values));
}

} // namespace

int main()
{
    // Scenario 1: compile a small object schema once, then reuse it.
    const auto port_schema = Obj({{"type", cfgx::Node("integer")},
                                  {"minimum", cfgx::Node(std::int64_t(1))},
                                  {"maximum", cfgx::Node(std::int64_t(65535))}});
    const auto svc_schema = Obj({{"type", cfgx::Node("object")},
                                 {"required", Arr({cfgx::Node("port")})},
                                 {"properties", Obj({{"port", port_schema}})}});
    const auto schema_root = Obj({{"type", cfgx::Node("object")},
                                  {"required", Arr({cfgx::Node("svc")})},
                                  {"additionalProperties", cfgx::Node(false)},
                                  {"properties", Obj({{"svc", svc_schema}})}});
    const auto schema = schemax::Compile(schema_root);
    if (!schema.ok)
    {
        std::cout << "schema-error=" << schema.error << "\n";
        return 1;
    }

    // Scenario 2: valid documents return an empty issue list.
    const auto valid = schemax::Validate(Obj({{"svc", Obj({{"port", cfgx::Node(std::int64_t(8080))}})}}), schema.value);
    std::cout << "valid-issues=" << valid.size() << "\n";

    // Scenario 3: invalid documents report path, code, and user-facing message.
    const auto invalid = schemax::Validate(
        Obj({{"svc", Obj({{"port", cfgx::Node(std::int64_t(70000))}})}, {"extra", cfgx::Node(true)}}), schema.value);
    for (const auto& issue : invalid)
    {
        std::cout << issue.path << " " << issue.code << " " << issue.message << "\n";
    }

    // Scenario 4: fail-fast is useful for interactive editors.
    schemax::Options options;
    options.fail_fast = true;
    std::cout << "fail-fast=" << schemax::Validate(Obj({}), schema.value, options).size() << "\n";

    // Scenario 5: cfgtool/toolx-sync can reuse cfgx-style validation issues.
    std::cout << "cfgx-issues=" << schemax::ToCfgxIssues(invalid).size() << "\n";
    return 0;
}
