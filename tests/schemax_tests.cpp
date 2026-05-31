#include "schemax.h"

#include <gtest/gtest.h>

namespace
{
cfgx::Node Obj(std::initializer_list<std::pair<std::string, cfgx::Node>> fields)
{
    cfgx::Node::Object obj;
    for (const auto& field : fields)
    {
        obj.push_back(field);
    }
    return cfgx::Node(std::move(obj));
}

cfgx::Node Arr(std::initializer_list<cfgx::Node> values)
{
    return cfgx::Node(cfgx::Node::Array(values));
}
} // namespace

TEST(SchemaxTests, CompileRejectsUnknownKeysAndBadShapes)
{
    const auto bad_key = schemax::Compile(Obj({{"unexpected", cfgx::Node(true)}}));
    EXPECT_FALSE(bad_key.ok);

    const auto bad_required = schemax::Compile(Obj({{"type", cfgx::Node("object")}, {"required", cfgx::Node("name")}}));
    EXPECT_FALSE(bad_required.ok);
}

TEST(SchemaxTests, ValidatesObjectPropertiesAndRequiredFields)
{
    const auto compiled = schemax::Compile(Obj({
        {"type", cfgx::Node("object")},
        {"required", Arr({cfgx::Node("svc")})},
        {"additionalProperties", cfgx::Node(false)},
        {"properties", Obj({{"svc", Obj({{"type", cfgx::Node("object")},
                                         {"required", Arr({cfgx::Node("port")})},
                                         {"properties", Obj({{"port", Obj({{"type", cfgx::Node("integer")}})}})}})}})},
    }));
    ASSERT_TRUE(compiled.ok) << compiled.error;

    const auto issues = schemax::Validate(Obj({{"extra", cfgx::Node(true)}, {"svc", Obj({})}}), compiled.value);
    ASSERT_EQ(issues.size(), 2u);
    EXPECT_EQ(issues[0].code, "required");
    EXPECT_EQ(issues[1].code, "additionalProperties");
}

TEST(SchemaxTests, ValidatesRangeEnumLengthAndArrayItems)
{
    const auto compiled = schemax::Compile(Obj({
        {"type", cfgx::Node("object")},
        {"properties",
         Obj({
             {"ports", Obj({{"type", cfgx::Node("array")},
                            {"items", Obj({{"type", cfgx::Node("integer")},
                                           {"minimum", cfgx::Node(std::int64_t(1))},
                                           {"maximum", cfgx::Node(std::int64_t(10))}})}})},
             {"mode", Obj({{"type", cfgx::Node("string")}, {"enum", Arr({cfgx::Node("dev"), cfgx::Node("prod")})}})},
             {"name", Obj({{"type", cfgx::Node("string")},
                           {"minLength", cfgx::Node(std::int64_t(2))},
                           {"maxLength", cfgx::Node(std::int64_t(4))}})},
         })},
    }));
    ASSERT_TRUE(compiled.ok) << compiled.error;

    const auto issues =
        schemax::Validate(Obj({{"ports", Arr({cfgx::Node(std::int64_t(0)), cfgx::Node(std::int64_t(11))})},
                               {"mode", cfgx::Node("qa")},
                               {"name", cfgx::Node("toolx")}}),
                          compiled.value);

    ASSERT_EQ(issues.size(), 4u);
    EXPECT_EQ(issues[0].path, "ports[0]");
    EXPECT_EQ(issues[0].code, "minimum");
    EXPECT_EQ(issues[1].path, "ports[1]");
    EXPECT_EQ(issues[1].code, "maximum");
    EXPECT_EQ(issues[2].code, "enum");
    EXPECT_EQ(issues[3].code, "maxLength");
}

TEST(SchemaxTests, FailFastAndCfgxIssueConversionWork)
{
    const auto compiled = schemax::Compile(Obj({
        {"type", cfgx::Node("object")},
        {"required", Arr({cfgx::Node("a"), cfgx::Node("b")})},
    }));
    ASSERT_TRUE(compiled.ok) << compiled.error;

    schemax::Options options;
    options.fail_fast = true;
    const auto issues = schemax::Validate(Obj({}), compiled.value, options);
    ASSERT_EQ(issues.size(), 1u);

    const auto cfgx_issues = schemax::ToCfgxIssues(issues);
    ASSERT_EQ(cfgx_issues.size(), 1u);
    EXPECT_NE(cfgx_issues[0].message.find("[required]"), std::string::npos);
}
