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
    EXPECT_NE(bad_key.error.find("unsupported schema key"), std::string::npos);

    const auto bad_required = schemax::Compile(Obj({{"type", cfgx::Node("object")}, {"required", cfgx::Node("name")}}));
    EXPECT_FALSE(bad_required.ok);
    EXPECT_NE(bad_required.error.find("required"), std::string::npos);
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

TEST(SchemaxTests, NestedPathsAndAdditionalPropertiesDefaultAreStable)
{
    const cfgx::Node integer_schema = Obj({{"type", cfgx::Node("integer")}});
    const cfgx::Node ports_schema = Obj({{"type", cfgx::Node("array")}, {"items", integer_schema}});
    const cfgx::Node svc_schema = Obj({{"type", cfgx::Node("object")}, {"properties", Obj({{"ports", ports_schema}})}});
    const auto compiled =
        schemax::Compile(Obj({{"type", cfgx::Node("object")}, {"properties", Obj({{"svc", svc_schema}})}}));
    ASSERT_TRUE(compiled.ok) << compiled.error;

    const auto issues =
        schemax::Validate(Obj({{"svc", Obj({{"ports", Arr({cfgx::Node(std::int64_t(1)), cfgx::Node("bad")})}})},
                               {"extra", cfgx::Node(true)}}),
                          compiled.value);
    ASSERT_EQ(issues.size(), 1u);
    EXPECT_EQ(issues[0].path, "svc.ports[1]");
    EXPECT_EQ(issues[0].code, "type");
}

TEST(SchemaxTests, EnumComparisonRespectsScalarTypes)
{
    const auto compiled = schemax::Compile(Obj({
        {"enum", Arr({cfgx::Node(std::int64_t(1)), cfgx::Node("1"), cfgx::Node(true)})},
    }));
    ASSERT_TRUE(compiled.ok) << compiled.error;

    EXPECT_TRUE(schemax::Validate(cfgx::Node(std::int64_t(1)), compiled.value).empty());
    EXPECT_TRUE(schemax::Validate(cfgx::Node("1"), compiled.value).empty());
    EXPECT_TRUE(schemax::Validate(cfgx::Node(true), compiled.value).empty());

    const auto mismatch = schemax::Validate(cfgx::Node(1.5), compiled.value);
    ASSERT_EQ(mismatch.size(), 1u);
    EXPECT_EQ(mismatch[0].code, "enum");
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

TEST(SchemaxTests, AdditionalPropertiesWithoutPropertiesRejectsUnknownMembers)
{
    for (const auto* text : {R"({"type":"object","additionalProperties":false})",
                             R"({"type":"object","properties":{},"additionalProperties":false})"})
    {
        const auto compiled = schemax::Compile(cfgx::ParseJson(text).value);
        ASSERT_TRUE(compiled.ok) << compiled.error;
        EXPECT_TRUE(schemax::Validate(Obj({}), compiled.value).empty());
        const auto document = Obj({{"a", cfgx::Node(true)}, {"b", cfgx::Node(false)}});
        for (bool fail_fast : {false, true})
        {
            schemax::Options options;
            options.fail_fast = fail_fast;
            const auto issues = schemax::Validate(document, compiled.value, options);
            ASSERT_EQ(issues.size(), fail_fast ? 1u : 2u);
            EXPECT_EQ(issues[0].path, "a");
            EXPECT_EQ(issues[0].code, "additionalProperties");
        }
    }
    const auto nested = schemax::Compile(
        cfgx::ParseJson(R"({"type":"object","properties":{"child":{"type":"object","additionalProperties":false}}})")
            .value);
    ASSERT_TRUE(nested.ok);
    const auto issues = schemax::Validate(Obj({{"child", Obj({{"extra", cfgx::Node(true)}})}}), nested.value);
    ASSERT_EQ(issues.size(), 1u);
    EXPECT_EQ(issues[0].path, "child.extra");
    const auto allowed = schemax::Compile(Obj({{"additionalProperties", cfgx::Node(true)}}));
    ASSERT_TRUE(allowed.ok);
    EXPECT_TRUE(schemax::Validate(Obj({{"extra", cfgx::Node(true)}}), allowed.value).empty());
}

TEST(SchemaxTests, CompileRejectsNegativeStringLengthsAndKeepsNonnegativeBounds)
{
    for (const auto* keyword : {"minLength", "maxLength"})
    {
        const auto invalid = schemax::Compile(Obj({{keyword, cfgx::Node(std::int64_t(-1))}}));
        EXPECT_FALSE(invalid.ok);
        EXPECT_NE(invalid.error.find(keyword), std::string::npos);
    }
    const auto zero = schemax::Compile(Obj({{"maxLength", cfgx::Node(std::int64_t(0))}}));
    ASSERT_TRUE(zero.ok);
    EXPECT_TRUE(schemax::Validate(cfgx::Node(""), zero.value).empty());
    EXPECT_EQ(schemax::Validate(cfgx::Node("a"), zero.value).size(), 1u);
    const auto exact =
        schemax::Compile(Obj({{"minLength", cfgx::Node(std::int64_t(3))}, {"maxLength", cfgx::Node(std::int64_t(3))}}));
    ASSERT_TRUE(exact.ok);
    EXPECT_TRUE(schemax::Validate(cfgx::Node("abc"), exact.value).empty());
    EXPECT_EQ(schemax::Validate(cfgx::Node("ab"), exact.value).size(), 1u);
    EXPECT_EQ(schemax::Validate(cfgx::Node("abcd"), exact.value).size(), 1u);
}
