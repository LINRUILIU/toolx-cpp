#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>

#include "cfgx.h"

namespace
{
    std::filesystem::path TestTempPath(const char *name)
    {
        const auto root = std::filesystem::current_path() / "toolx_test_tmp";
        std::filesystem::create_directories(root);
        return root / name;
    }

    class ParserAdapterScope
    {
    public:
        ParserAdapterScope()
        {
            cfgx::ClearParserAdapters();
        }

        ~ParserAdapterScope()
        {
            cfgx::ClearParserAdapters();
        }
    };

    class RemoteFetcherScope
    {
    public:
        RemoteFetcherScope()
        {
            cfgx::SetRemoteFetcher({});
        }

        ~RemoteFetcherScope()
        {
            cfgx::SetRemoteFetcher({});
        }
    };

    cfgx::ParserAdapter MakeFailingAdapter(std::string name)
    {
        cfgx::ParserAdapter adapter;
        adapter.name = std::move(name);
        adapter.parse = [](std::string_view, cfgx::ConfigFormat)
        {
            cfgx::Result<cfgx::Node> out;
            out.ok = false;
            out.error = "adapter parse failed";
            return out;
        };
        adapter.dump = [](const cfgx::Node &, cfgx::ConfigFormat, int)
        {
            cfgx::Result<std::string> out;
            out.ok = false;
            out.error = "adapter dump failed";
            return out;
        };
        return adapter;
    }

} // namespace

TEST(CfgxPathTests, ParsePathSupportsEscapedSegments)
{
    const auto parsed = cfgx::ParsePath("service\\.core.nodes[2].name");
    ASSERT_TRUE(parsed.ok) << parsed.error;
    ASSERT_EQ(parsed.value.size(), 4U);

    EXPECT_EQ(parsed.value[0].kind, cfgx::PathTokenKind::Key);
    EXPECT_EQ(parsed.value[0].key, "service.core");

    EXPECT_EQ(parsed.value[1].kind, cfgx::PathTokenKind::Key);
    EXPECT_EQ(parsed.value[1].key, "nodes");

    EXPECT_EQ(parsed.value[2].kind, cfgx::PathTokenKind::Index);
    EXPECT_EQ(parsed.value[2].index, 2U);

    EXPECT_EQ(parsed.value[3].kind, cfgx::PathTokenKind::Key);
    EXPECT_EQ(parsed.value[3].key, "name");
}

TEST(CfgxPathTests, ParsePathRejectsTrailingEscape)
{
    const auto parsed = cfgx::ParsePath("a\\");
    ASSERT_FALSE(parsed.ok);
    EXPECT_NE(parsed.error.find("trailing escape"), std::string::npos);
}

TEST(CfgxPathTests, ParsePathSupportsDotAfterIndexAndRejectsTrailingDot)
{
    const auto ok = cfgx::ParsePath("servers[0].host");
    ASSERT_TRUE(ok.ok) << ok.error;
    ASSERT_EQ(ok.value.size(), 3U);
    EXPECT_EQ(ok.value[0].kind, cfgx::PathTokenKind::Key);
    EXPECT_EQ(ok.value[1].kind, cfgx::PathTokenKind::Index);
    EXPECT_EQ(ok.value[2].kind, cfgx::PathTokenKind::Key);

    const auto bad = cfgx::ParsePath("servers[0].");
    ASSERT_FALSE(bad.ok);
    EXPECT_NE(bad.error.find("trailing '.'"), std::string::npos);
}

TEST(CfgxTreeTests, SetGetExistsAndRemoveWork)
{
    cfgx::Node root;

    auto st = cfgx::SetNode(root, "app.settings.enabled", cfgx::Node(true));
    ASSERT_TRUE(st.ok) << st.error;

    st = cfgx::SetNode(root, "app.settings.retries", cfgx::Node(std::int64_t(3)));
    ASSERT_TRUE(st.ok) << st.error;

    st = cfgx::SetNode(root, "app.list[1]", cfgx::Node("node-1"));
    ASSERT_TRUE(st.ok) << st.error;

    const auto enabled = cfgx::GetNode(root, "app.settings.enabled");
    ASSERT_TRUE(enabled.ok) << enabled.error;
    ASSERT_NE(enabled.value, nullptr);
    EXPECT_TRUE(enabled.value->AsBool());

    const auto retries = cfgx::GetNode(root, "app.settings.retries");
    ASSERT_TRUE(retries.ok) << retries.error;
    EXPECT_EQ(retries.value->AsInt(), 3);

    const auto item = cfgx::GetNode(root, "app.list[1]");
    ASSERT_TRUE(item.ok) << item.error;
    EXPECT_EQ(item.value->AsString(), "node-1");

    EXPECT_TRUE(cfgx::Exists(root, "app.settings.enabled"));
    EXPECT_FALSE(cfgx::Exists(root, "app.settings.missing"));

    st = cfgx::RemoveNode(root, "app.settings.retries");
    ASSERT_TRUE(st.ok) << st.error;
    EXPECT_FALSE(cfgx::Exists(root, "app.settings.retries"));
}

TEST(CfgxValueTests, AsIntSupportsIntegralDouble)
{
    const cfgx::Node whole_double(8080.0);
    EXPECT_EQ(whole_double.AsInt(-1), 8080);

    const cfgx::Node non_whole_double(8080.5);
    EXPECT_EQ(non_whole_double.AsInt(-1), -1);
}

TEST(CfgxNodeApiTests, ConvenienceObjectAndArrayApisWork)
{
    cfgx::Node obj = cfgx::Node::MakeObject();
    ASSERT_TRUE(obj.Set("name", cfgx::Node("demo")).ok);
    ASSERT_TRUE(obj.Set("port", cfgx::Node(std::int64_t(8080))).ok);

    const auto *name = obj.Get("name");
    ASSERT_NE(name, nullptr);
    EXPECT_EQ(name->AsString(), "demo");

    ASSERT_TRUE(obj.Erase("name").ok);
    EXPECT_EQ(obj.Get("name"), nullptr);

    cfgx::Node arr = cfgx::Node::MakeArray();
    ASSERT_TRUE(arr.Push(cfgx::Node("a")).ok);
    ASSERT_TRUE(arr.SetAt(2, cfgx::Node("c"), true).ok);
    EXPECT_EQ(arr.Size(), 3U);

    const auto *at2 = arr.At(2);
    ASSERT_NE(at2, nullptr);
    EXPECT_EQ(at2->AsString(), "c");

    EXPECT_FALSE(arr.SetAt(5, cfgx::Node("x"), false).ok);
    ASSERT_TRUE(arr.EraseAt(1).ok);
    EXPECT_EQ(arr.Size(), 2U);
}

TEST(CfgxValueTests, UnifiedEmptyCheckCoversCommonEmptyShapes)
{
    const cfgx::Node null_node;
    EXPECT_TRUE(null_node.IsEmpty());
    EXPECT_TRUE(cfgx::IsEmptyValue(null_node));

    const cfgx::Node empty_string("");
    EXPECT_TRUE(empty_string.IsEmpty());

    const cfgx::Node non_empty_string("x");
    EXPECT_FALSE(non_empty_string.IsEmpty());

    const cfgx::Node empty_array = cfgx::Node::MakeArray();
    EXPECT_TRUE(empty_array.IsEmpty());

    const cfgx::Node empty_object = cfgx::Node::MakeObject();
    EXPECT_TRUE(empty_object.IsEmpty());

    const cfgx::Node zero_int(std::int64_t(0));
    EXPECT_FALSE(zero_int.IsEmpty());
}

TEST(CfgxV2ComposeTests, BuildEnvLayerFromPairsSupportsPolicyPrecedence)
{
    std::vector<std::pair<std::string, std::string>> env_vars = {
        {"APP_CFG_SERVER__HOST", "127.0.0.1"},
        {"APP_CFG_SERVER__PORT", "8080"},
        {"OTHER_KEY", "ignored"},
    };

    cfgx::TypePolicyOptions policy;
    policy.strict_string_global = false;
    policy.strict_string_by_source.push_back({cfgx::SourceLayer::Env, true});
    policy.strict_string_by_path.push_back({"server.port", false});

    const auto layer = cfgx::BuildEnvLayerFromPairs(env_vars, "APP_CFG_", policy);
    ASSERT_TRUE(layer.ok) << layer.error;

    const auto host = cfgx::GetNode(layer.value, "server.host");
    ASSERT_TRUE(host.ok) << host.error;
    EXPECT_EQ(host.value->Kind(), cfgx::NodeKind::String);
    EXPECT_EQ(host.value->AsString(), "127.0.0.1");

    const auto port = cfgx::GetNode(layer.value, "server.port");
    ASSERT_TRUE(port.ok) << port.error;
    EXPECT_EQ(port.value->Kind(), cfgx::NodeKind::Integer);
    EXPECT_EQ(port.value->AsInt(-1), 8080);
}

TEST(CfgxV2ComposeTests, RuntimeOverridesApplyLastWriteWins)
{
    cfgx::Node base = cfgx::Node::MakeObject();
    ASSERT_TRUE(cfgx::SetNode(base, "svc.port", cfgx::Node(std::int64_t(1000))).ok);

    cfgx::RuntimeOverrides runtime;
    ASSERT_TRUE(runtime.Set("svc.port", cfgx::Node(std::int64_t(7000))).ok);

    cfgx::Node replaced = cfgx::Node::MakeObject();
    ASSERT_TRUE(cfgx::SetNode(replaced, "svc.port", cfgx::Node(std::int64_t(8000))).ok);
    ASSERT_TRUE(runtime.Replace(replaced).ok);
    ASSERT_TRUE(runtime.Set("svc.port", cfgx::Node(std::int64_t(9000))).ok);

    const auto composed = cfgx::ComposeLayers(base, std::nullopt, std::nullopt, &runtime);
    ASSERT_TRUE(composed.ok) << composed.error;

    const auto port = cfgx::GetNode(composed.value, "svc.port");
    ASSERT_TRUE(port.ok) << port.error;
    EXPECT_EQ(port.value->AsInt(-1), 9000);
}

TEST(CfgxV2ComposeTests, ComposeLayersProvidesSourceAttribution)
{
    cfgx::Node base = cfgx::Node::MakeObject();
    ASSERT_TRUE(cfgx::SetNode(base, "svc.port", cfgx::Node(std::int64_t(1000))).ok);

    cfgx::Node env = cfgx::Node::MakeObject();
    ASSERT_TRUE(cfgx::SetNode(env, "svc.port", cfgx::Node(std::int64_t(2000))).ok);

    cfgx::Node local = cfgx::Node::MakeObject();
    ASSERT_TRUE(cfgx::SetNode(local, "svc.host", cfgx::Node("127.0.0.1")).ok);

    cfgx::RuntimeOverrides runtime;
    ASSERT_TRUE(runtime.Set("svc.port", cfgx::Node(std::int64_t(3000))).ok);

    std::vector<cfgx::SourceAttribution> trace;
    const auto composed = cfgx::ComposeLayers(base, env, local, &runtime, cfgx::ComposeOptions{}, &trace);
    ASSERT_TRUE(composed.ok) << composed.error;

    const auto port = cfgx::GetNode(composed.value, "svc.port");
    ASSERT_TRUE(port.ok) << port.error;
    EXPECT_EQ(port.value->AsInt(-1), 3000);

    const auto host = cfgx::GetNode(composed.value, "svc.host");
    ASSERT_TRUE(host.ok) << host.error;
    EXPECT_EQ(host.value->AsString(), "127.0.0.1");

    auto find_layer = [&trace](const std::string &path) -> std::optional<cfgx::SourceLayer>
    {
        for (const auto &entry : trace)
        {
            if (entry.path == path)
            {
                return entry.layer;
            }
        }
        return std::nullopt;
    };

    const auto port_layer = find_layer("svc.port");
    ASSERT_TRUE(port_layer.has_value());
    EXPECT_EQ(*port_layer, cfgx::SourceLayer::Runtime);

    const auto host_layer = find_layer("svc.host");
    ASSERT_TRUE(host_layer.has_value());
    EXPECT_EQ(*host_layer, cfgx::SourceLayer::Local);
}

TEST(CfgxV2ReloadTests, TickSupportsDebounceAndRollback)
{
    const auto file = TestTempPath("cfgx_v2_reload_debounce.json");
    {
        std::ofstream out(file.string(), std::ios::trunc);
        out << R"({"svc":{"port":1000}})";
    }

    cfgx::PollReloader reloader(file.string());
    cfgx::ReloadOptions options;
    options.debounce_ms = 200;
    options.include_snapshots = true;
    reloader.SetOptions(options);

    auto init = reloader.ReloadNow();
    ASSERT_TRUE(init.ok) << init.error;
    ASSERT_TRUE(init.value.attempted);

    cfgx::ReloadEvent captured;
    bool callback_called = false;
    reloader.SetCallback([&](const cfgx::ReloadEvent &event)
                         {
                             callback_called = true;
                             captured = event; });

    {
        std::ofstream out(file.string(), std::ios::trunc);
        out << R"({"svc":)";
    }

    auto t1 = reloader.Tick(1000);
    ASSERT_TRUE(t1.ok) << t1.error;
    EXPECT_FALSE(t1.value.attempted);

    auto t2 = reloader.Tick(1100);
    ASSERT_TRUE(t2.ok) << t2.error;
    EXPECT_FALSE(t2.value.attempted);

    auto t3 = reloader.Tick(1301);
    ASSERT_TRUE(t3.ok) << t3.error;
    EXPECT_TRUE(t3.value.attempted);
    EXPECT_TRUE(t3.value.rolled_back);
    EXPECT_FALSE(t3.value.changed);

    const auto *current = reloader.Current();
    ASSERT_NE(current, nullptr);
    const auto port = cfgx::GetNode(*current, "svc.port");
    ASSERT_TRUE(port.ok) << port.error;
    EXPECT_EQ(port.value->AsInt(-1), 1000);

    EXPECT_TRUE(callback_called);
    EXPECT_TRUE(captured.attempted);
    EXPECT_TRUE(captured.rolled_back);
    EXPECT_TRUE(captured.old_snapshot.has_value());
    EXPECT_FALSE(captured.new_snapshot.has_value());
    EXPECT_TRUE(captured.diff_paths.empty());
    EXPECT_FALSE(captured.source_trace.empty());

    std::error_code ec;
    std::filesystem::remove(file, ec);
}

TEST(CfgxV2ReloadTests, MtimeDriftWithSameContentDoesNotTriggerReload)
{
    const auto file = TestTempPath("cfgx_v2_reload_same_content.json");
    const std::string text = R"({"svc":{"port":1000}})";
    {
        std::ofstream out(file.string(), std::ios::trunc);
        out << text;
    }

    cfgx::PollReloader reloader(file.string());
    cfgx::ReloadOptions options;
    options.debounce_ms = 0;
    reloader.SetOptions(options);

    auto init = reloader.ReloadNow();
    ASSERT_TRUE(init.ok) << init.error;

    {
        std::ofstream out(file.string(), std::ios::trunc);
        out << text;
    }

    auto tick = reloader.Tick(2000);
    ASSERT_TRUE(tick.ok) << tick.error;
    EXPECT_FALSE(tick.value.attempted);
    EXPECT_EQ(tick.value.message, "no file changes detected");

    std::error_code ec;
    std::filesystem::remove(file, ec);
}

TEST(CfgxV2ReloadTests, CallbackCarriesDiffAndSourceTrace)
{
    const auto file = TestTempPath("cfgx_v2_reload_callback.json");
    {
        std::ofstream out(file.string(), std::ios::trunc);
        out << R"({"svc":{"port":1000,"host":"127.0.0.1"}})";
    }

    cfgx::PollReloader reloader(file.string());
    cfgx::ReloadOptions options;
    options.debounce_ms = 0;
    options.include_snapshots = true;
    reloader.SetOptions(options);

    auto init = reloader.ReloadNow();
    ASSERT_TRUE(init.ok) << init.error;

    cfgx::ReloadEvent captured;
    bool callback_called = false;
    reloader.SetCallback([&](const cfgx::ReloadEvent &event)
                         {
                             callback_called = true;
                             captured = event; });

    {
        std::ofstream out(file.string(), std::ios::trunc);
        out << R"({"svc":{"port":2000,"host":"127.0.0.1"}})";
    }

    auto tick = reloader.Tick(3000);
    ASSERT_TRUE(tick.ok) << tick.error;
    ASSERT_TRUE(tick.value.attempted);
    ASSERT_TRUE(tick.value.changed);
    EXPECT_FALSE(tick.value.rolled_back);

    EXPECT_TRUE(callback_called);
    EXPECT_TRUE(captured.old_snapshot.has_value());
    EXPECT_TRUE(captured.new_snapshot.has_value());
    EXPECT_FALSE(captured.diff_paths.empty());

    bool has_port_diff = false;
    for (const auto &entry : captured.diff_paths)
    {
        if (entry.path == "svc.port")
        {
            has_port_diff = true;
            EXPECT_EQ(entry.kind, cfgx::DiffKind::Changed);
            EXPECT_EQ(entry.layer, cfgx::SourceLayer::Base);
        }
    }
    EXPECT_TRUE(has_port_diff);

    bool has_port_source = false;
    for (const auto &entry : captured.source_trace)
    {
        if (entry.path == "svc.port")
        {
            has_port_source = true;
            EXPECT_EQ(entry.layer, cfgx::SourceLayer::Base);
        }
    }
    EXPECT_TRUE(has_port_source);

    std::error_code ec;
    std::filesystem::remove(file, ec);
}

TEST(CfgxMergeTests, MergeOverridesObjectsRecursively)
{
    cfgx::Node base = cfgx::Node::MakeObject();
    ASSERT_TRUE(cfgx::SetNode(base, "svc.host", cfgx::Node("127.0.0.1")).ok);
    ASSERT_TRUE(cfgx::SetNode(base, "svc.port", cfgx::Node(std::int64_t(8000))).ok);

    cfgx::Node overlay = cfgx::Node::MakeObject();
    ASSERT_TRUE(cfgx::SetNode(overlay, "svc.port", cfgx::Node(std::int64_t(9000))).ok);
    ASSERT_TRUE(cfgx::SetNode(overlay, "svc.tls", cfgx::Node(true)).ok);

    const auto st = cfgx::Merge(base, overlay);
    ASSERT_TRUE(st.ok) << st.error;

    const auto host = cfgx::GetNode(base, "svc.host");
    ASSERT_TRUE(host.ok);
    EXPECT_EQ(host.value->AsString(), "127.0.0.1");

    const auto port = cfgx::GetNode(base, "svc.port");
    ASSERT_TRUE(port.ok);
    EXPECT_EQ(port.value->AsInt(), 9000);

    const auto tls = cfgx::GetNode(base, "svc.tls");
    ASSERT_TRUE(tls.ok);
    EXPECT_TRUE(tls.value->AsBool());
}

TEST(CfgxJsonTests, ParseAndDumpJsonRoundTrip)
{
    const auto parsed = cfgx::ParseJson(R"({"a":1,"b":[true,"x"],"c":{"k":"v"}})");
    ASSERT_TRUE(parsed.ok) << parsed.error;

    const auto node = cfgx::GetNode(parsed.value, "c.k");
    ASSERT_TRUE(node.ok) << node.error;
    EXPECT_EQ(node.value->AsString(), "v");

    const std::string dumped = cfgx::ToJson(parsed.value, 2);
    EXPECT_NE(dumped.find("\"b\""), std::string::npos);
    EXPECT_NE(dumped.find("\"k\""), std::string::npos);
}

TEST(CfgxValidationTests, ValidateCollectsIssues)
{
    cfgx::Node root = cfgx::Node::MakeObject();
    ASSERT_TRUE(cfgx::SetNode(root, "svc.port", cfgx::Node(std::int64_t(70000))).ok);

    cfgx::ValidationRule range_rule;
    range_rule.name = "port-range";
    range_rule.fail_fast = false;
    range_rule.evaluator = [](const cfgx::Node &cfg) -> std::optional<cfgx::ValidationIssue>
    {
        const auto value = cfgx::GetNode(cfg, "svc.port");
        if (!value.ok || value.value == nullptr)
        {
            return cfgx::ValidationIssue{"svc.port", "missing port"};
        }

        const auto port = value.value->AsInt(-1);
        if (port < 1 || port > 65535)
        {
            return cfgx::ValidationIssue{"svc.port", "port must be in [1,65535]"};
        }
        return std::nullopt;
    };

    const auto validation = cfgx::Validate(root, {range_rule});
    ASSERT_FALSE(validation.ok);
    ASSERT_EQ(validation.value.size(), 1U);
    EXPECT_EQ(validation.value[0].path, "svc.port");
    EXPECT_NE(validation.value[0].message.find("port-range"), std::string::npos);
}

TEST(CfgxIoTests, LoadAndSaveJsonFileWork)
{
    const auto file = TestTempPath("cfgx_io_test.json");

    cfgx::Node root = cfgx::Node::MakeObject();
    ASSERT_TRUE(cfgx::SetNode(root, "demo.value", cfgx::Node("ok")).ok);

    auto save = cfgx::SaveToFile(root, file.string());
    ASSERT_TRUE(save.ok) << save.error;

    auto load = cfgx::LoadFromFile(file.string());
    ASSERT_TRUE(load.ok) << load.error;

    auto value = cfgx::GetNode(load.value, "demo.value");
    ASSERT_TRUE(value.ok) << value.error;
    EXPECT_EQ(value.value->AsString(), "ok");

    std::error_code ec;
    std::filesystem::remove(file, ec);
}

TEST(CfgxIniTests, LoadIniWithSectionsAndScalars)
{
    const auto file = TestTempPath("cfgx_ini_load_test.ini");
    {
        std::ofstream out(file.string(), std::ios::trunc);
        out << R"(name = demo
[server]
host = 127.0.0.1
port = 8080
tls = true
)";
    }

    auto load = cfgx::LoadFromFile(file.string());
    ASSERT_TRUE(load.ok) << load.error;

    auto name = cfgx::GetNode(load.value, "name");
    ASSERT_TRUE(name.ok) << name.error;
    EXPECT_EQ(name.value->AsString(), "demo");

    auto host = cfgx::GetNode(load.value, "server.host");
    ASSERT_TRUE(host.ok) << host.error;
    EXPECT_EQ(host.value->AsString(), "127.0.0.1");

    auto port = cfgx::GetNode(load.value, "server.port");
    ASSERT_TRUE(port.ok) << port.error;
    EXPECT_EQ(port.value->AsInt(), 8080);

    auto tls = cfgx::GetNode(load.value, "server.tls");
    ASSERT_TRUE(tls.ok) << tls.error;
    EXPECT_TRUE(tls.value->AsBool());

    std::error_code ec;
    std::filesystem::remove(file, ec);
}

TEST(CfgxIniTests, SaveIniAndReloadWork)
{
    const auto file = TestTempPath("cfgx_ini_save_test.ini");

    cfgx::Node root = cfgx::Node::MakeObject();
    ASSERT_TRUE(cfgx::SetNode(root, "meta.env", cfgx::Node("prod")).ok);
    ASSERT_TRUE(cfgx::SetNode(root, "server.port", cfgx::Node(std::int64_t(9000))).ok);

    auto save = cfgx::SaveToFile(root, file.string());
    ASSERT_TRUE(save.ok) << save.error;

    auto load = cfgx::LoadFromFile(file.string());
    ASSERT_TRUE(load.ok) << load.error;

    auto env = cfgx::GetNode(load.value, "meta.env");
    ASSERT_TRUE(env.ok) << env.error;
    EXPECT_EQ(env.value->AsString(), "prod");

    auto port = cfgx::GetNode(load.value, "server.port");
    ASSERT_TRUE(port.ok) << port.error;
    EXPECT_EQ(port.value->AsInt(), 9000);

    std::error_code ec;
    std::filesystem::remove(file, ec);
}

TEST(CfgxIniTests, SaveIniPreservesCommentsOnWriteback)
{
    const auto file = TestTempPath("cfgx_ini_comment_preserve.ini");
    {
        std::ofstream out(file.string(), std::ios::trunc);
        out << R"(; global comment
[server]
port = 8080 ; inline comment
host = 127.0.0.1
)";
    }

    auto load = cfgx::LoadFromFile(file.string());
    ASSERT_TRUE(load.ok) << load.error;

    ASSERT_TRUE(cfgx::SetNode(load.value, "server.port", cfgx::Node(std::int64_t(9090))).ok);
    auto save = cfgx::SaveToFile(load.value, file.string());
    ASSERT_TRUE(save.ok) << save.error;

    std::ifstream in(file.string(), std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    EXPECT_NE(text.find("; global comment"), std::string::npos);
    EXPECT_NE(text.find("port = 9090 ; inline comment"), std::string::npos);

    std::error_code ec;
    std::filesystem::remove(file, ec);
}

TEST(CfgxYamlTests, LoadYamlSubsetWorks)
{
    const auto file = TestTempPath("cfgx_yaml_load_test.yaml");
    {
        std::ofstream out(file.string(), std::ios::trunc);
        out << R"(app:
  name: demo
  enabled: true
  retries: 3
servers:
  - host: 127.0.0.1
    port: 8080
  - host: 127.0.0.2
    port: 8081
)";
    }

    auto load = cfgx::LoadFromFile(file.string());
    ASSERT_TRUE(load.ok) << load.error;

    auto name = cfgx::GetNode(load.value, "app.name");
    ASSERT_TRUE(name.ok) << name.error;
    EXPECT_EQ(name.value->AsString(), "demo");

    auto enabled = cfgx::GetNode(load.value, "app.enabled");
    ASSERT_TRUE(enabled.ok) << enabled.error;
    EXPECT_TRUE(enabled.value->AsBool());

    auto port = cfgx::GetNode(load.value, "servers[1].port");
    ASSERT_TRUE(port.ok) << port.error;
    EXPECT_EQ(port.value->AsInt(), 8081);

    std::error_code ec;
    std::filesystem::remove(file, ec);
}

TEST(CfgxYamlTests, SaveYamlAndReloadWork)
{
    const auto file = TestTempPath("cfgx_yaml_save_test.yaml");

    cfgx::Node root = cfgx::Node::MakeObject();
    ASSERT_TRUE(cfgx::SetNode(root, "service.name", cfgx::Node("alpha")).ok);
    ASSERT_TRUE(cfgx::SetNode(root, "service.backends[0].host", cfgx::Node("127.0.0.10")).ok);
    ASSERT_TRUE(cfgx::SetNode(root, "service.backends[0].port", cfgx::Node(std::int64_t(7001))).ok);

    auto save = cfgx::SaveToFile(root, file.string());
    ASSERT_TRUE(save.ok) << save.error;

    auto load = cfgx::LoadFromFile(file.string());
    ASSERT_TRUE(load.ok) << load.error;

    auto backend_host = cfgx::GetNode(load.value, "service.backends[0].host");
    ASSERT_TRUE(backend_host.ok) << backend_host.error;
    EXPECT_EQ(backend_host.value->AsString(), "127.0.0.10");

    auto backend_port = cfgx::GetNode(load.value, "service.backends[0].port");
    ASSERT_TRUE(backend_port.ok) << backend_port.error;
    EXPECT_EQ(backend_port.value->AsInt(), 7001);

    std::error_code ec;
    std::filesystem::remove(file, ec);
}

TEST(CfgxYamlTests, SaveYamlPreservesCommentsForScalarMappings)
{
    const auto file = TestTempPath("cfgx_yaml_comment_preserve.yaml");
    {
        std::ofstream out(file.string(), std::ios::trunc);
        out << R"(# app config
service:
  host: "127.0.0.1" # keep this
  port: 8080
)";
    }

    auto load = cfgx::LoadFromFile(file.string());
    ASSERT_TRUE(load.ok) << load.error;

    ASSERT_TRUE(cfgx::SetNode(load.value, "service.host", cfgx::Node("10.0.0.8")).ok);
    auto save = cfgx::SaveToFile(load.value, file.string());
    ASSERT_TRUE(save.ok) << save.error;

    std::ifstream in(file.string(), std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    EXPECT_NE(text.find("# app config"), std::string::npos);
    EXPECT_NE(text.find("host: \"10.0.0.8\" # keep this"), std::string::npos);

    std::error_code ec;
    std::filesystem::remove(file, ec);
}

TEST(CfgxValidationFactoryTests, ParseNodeKindSupportsAliases)
{
    const auto int_kind = cfgx::ParseNodeKind("int");
    ASSERT_TRUE(int_kind.has_value());
    EXPECT_EQ(*int_kind, cfgx::NodeKind::Integer);

    const auto map_kind = cfgx::ParseNodeKind("map");
    ASSERT_TRUE(map_kind.has_value());
    EXPECT_EQ(*map_kind, cfgx::NodeKind::Object);

    const auto list_kind = cfgx::ParseNodeKind("list");
    ASSERT_TRUE(list_kind.has_value());
    EXPECT_EQ(*list_kind, cfgx::NodeKind::Array);

    const auto unknown = cfgx::ParseNodeKind("weird");
    EXPECT_FALSE(unknown.has_value());
}

TEST(CfgxValidationFactoryTests, BuiltinRulesDetectIssues)
{
    cfgx::Node root = cfgx::Node::MakeObject();
    ASSERT_TRUE(cfgx::SetNode(root, "svc.port", cfgx::Node(std::int64_t(70000))).ok);
    ASSERT_TRUE(cfgx::SetNode(root, "svc.mode", cfgx::Node("prod")).ok);

    std::vector<cfgx::ValidationRule> rules;
    rules.push_back(cfgx::RequirePathRule("svc.host"));
    rules.push_back(cfgx::ExpectKindRule("svc.mode", cfgx::NodeKind::Integer));
    rules.push_back(cfgx::NumericRangeRule("svc.port", 1, 65535));

    const auto validation = cfgx::Validate(root, rules);
    ASSERT_FALSE(validation.ok);
    EXPECT_EQ(validation.value.size(), 3U);
    EXPECT_EQ(validation.value[0].path, "svc.host");
    EXPECT_EQ(validation.value[1].path, "svc.mode");
    EXPECT_EQ(validation.value[2].path, "svc.port");
}

TEST(CfgxValidationFactoryTests, FailFastStopsAtFirstIssue)
{
    cfgx::Node root = cfgx::Node::MakeObject();
    ASSERT_TRUE(cfgx::SetNode(root, "svc.port", cfgx::Node(std::int64_t(70000))).ok);

    std::vector<cfgx::ValidationRule> rules;
    rules.push_back(cfgx::RequirePathRule("svc.host", true));
    rules.push_back(cfgx::ExpectKindRule("svc.mode", cfgx::NodeKind::Integer, true));
    rules.push_back(cfgx::NumericRangeRule("svc.port", 1, 65535, true));

    const auto validation = cfgx::Validate(root, rules);
    ASSERT_FALSE(validation.ok);
    ASSERT_EQ(validation.value.size(), 1U);
    EXPECT_EQ(validation.value[0].path, "svc.host");
}

TEST(CfgxValidationFactoryTests, ExtendedRulesDetectIssues)
{
    cfgx::Node root = cfgx::Node::MakeObject();
    ASSERT_TRUE(cfgx::SetNode(root, "svc.mode", cfgx::Node("prod")).ok);
    ASSERT_TRUE(cfgx::SetNode(root, "svc.token", cfgx::Node("a")).ok);
    ASSERT_TRUE(cfgx::SetNode(root, "svc.password", cfgx::Node("b")).ok);
    ASSERT_TRUE(cfgx::SetNode(root, "svc.tls", cfgx::Node(true)).ok);
    ASSERT_TRUE(cfgx::SetNode(root, "svc.name", cfgx::Node("xy")).ok);

    std::vector<cfgx::ValidationRule> rules;
    rules.push_back(cfgx::ChoiceRule("svc.mode", {"dev", "test"}));
    rules.push_back(cfgx::MutexRule({"svc.token", "svc.password"}));
    rules.push_back(cfgx::DependencyRule("svc.tls", "svc.cert"));
    rules.push_back(cfgx::StringLengthRule("svc.name", 3, 16));

    const auto validation = cfgx::Validate(root, rules);
    ASSERT_FALSE(validation.ok);
    ASSERT_EQ(validation.value.size(), 4U);
    EXPECT_EQ(validation.value[0].path, "svc.mode");
    EXPECT_EQ(validation.value[1].path, "svc.token");
    EXPECT_EQ(validation.value[2].path, "svc.tls");
    EXPECT_EQ(validation.value[3].path, "svc.name");
}

TEST(CfgxValidationFactoryTests, ExtendedRulesPassWhenValid)
{
    cfgx::Node root = cfgx::Node::MakeObject();
    ASSERT_TRUE(cfgx::SetNode(root, "svc.mode", cfgx::Node("dev")).ok);
    ASSERT_TRUE(cfgx::SetNode(root, "svc.token", cfgx::Node("t")).ok);
    ASSERT_TRUE(cfgx::SetNode(root, "svc.tls", cfgx::Node(true)).ok);
    ASSERT_TRUE(cfgx::SetNode(root, "svc.cert", cfgx::Node("cert.pem")).ok);
    ASSERT_TRUE(cfgx::SetNode(root, "svc.name", cfgx::Node("service-name")).ok);

    std::vector<cfgx::ValidationRule> rules;
    rules.push_back(cfgx::ChoiceRule("svc.mode", {"dev", "test"}));
    rules.push_back(cfgx::MutexRule({"svc.token", "svc.password"}));
    rules.push_back(cfgx::DependencyRule("svc.tls", "svc.cert"));
    rules.push_back(cfgx::StringLengthRule("svc.name", 3, 32));

    const auto validation = cfgx::Validate(root, rules);
    ASSERT_TRUE(validation.ok) << validation.error;
    EXPECT_TRUE(validation.value.empty());
}

TEST(CfgxV2SnapshotTests, PollReloaderSnapshotRestoreWorks)
{
    const auto file = TestTempPath("cfgx_v2_snapshot_restore.json");
    {
        std::ofstream out(file.string(), std::ios::trunc);
        out << R"({"svc":{"port":1000}})";
    }

    cfgx::PollReloader reloader(file.string());
    cfgx::ReloadOptions options;
    options.debounce_ms = 0;
    reloader.SetOptions(options);

    const auto init = reloader.ReloadNow();
    ASSERT_TRUE(init.ok) << init.error;

    const auto snapshot = reloader.SnapshotCurrent();
    ASSERT_TRUE(snapshot.ok) << snapshot.error;

    {
        std::ofstream out(file.string(), std::ios::trunc);
        out << R"({"svc":{"port":2000}})";
    }
    const auto tick = reloader.Tick(5000);
    ASSERT_TRUE(tick.ok) << tick.error;
    ASSERT_TRUE(tick.value.attempted);
    ASSERT_TRUE(tick.value.changed);

    const auto changed = reloader.Current();
    ASSERT_NE(changed, nullptr);
    const auto changed_port = cfgx::GetNode(*changed, "svc.port");
    ASSERT_TRUE(changed_port.ok) << changed_port.error;
    EXPECT_EQ(changed_port.value->AsInt(-1), 2000);

    ASSERT_TRUE(reloader.RestoreSnapshot(snapshot.value, &init.value.source_trace).ok);
    const auto restored = reloader.Current();
    ASSERT_NE(restored, nullptr);
    const auto restored_port = cfgx::GetNode(*restored, "svc.port");
    ASSERT_TRUE(restored_port.ok) << restored_port.error;
    EXPECT_EQ(restored_port.value->AsInt(-1), 1000);

    const auto *trace = reloader.CurrentSourceTrace();
    ASSERT_NE(trace, nullptr);
    EXPECT_FALSE(trace->empty());

    std::error_code ec;
    std::filesystem::remove(file, ec);
}

TEST(CfgxV2SnapshotTests, SnapshotFileExportImportAndAuditTrailWork)
{
    const auto file = TestTempPath("cfgx_v2_snapshot_file.json");
    const auto snapshot_file = TestTempPath("cfgx_v2_snapshot_export.json");
    {
        std::ofstream out(file.string(), std::ios::trunc);
        out << R"({"svc":{"port":1000}})";
    }

    cfgx::PollReloader reloader(file.string());
    cfgx::ReloadOptions options;
    options.debounce_ms = 0;
    reloader.SetOptions(options);

    const auto init = reloader.ReloadNow();
    ASSERT_TRUE(init.ok) << init.error;

    ASSERT_TRUE(reloader.ExportSnapshotToFile(snapshot_file.string(), cfgx::ConfigFormat::Json, 2).ok);
    const auto imported = reloader.ImportSnapshotFromFile(snapshot_file.string(), cfgx::ConfigFormat::Json);
    ASSERT_TRUE(imported.ok) << imported.error;

    {
        std::ofstream out(file.string(), std::ios::trunc);
        out << R"({"svc":)";
    }

    const auto tick = reloader.Tick(5000);
    ASSERT_TRUE(tick.ok) << tick.error;
    EXPECT_TRUE(tick.value.attempted);
    EXPECT_TRUE(tick.value.rolled_back);

    ASSERT_TRUE(reloader.RestoreSnapshotFromFile(snapshot_file.string(), cfgx::ConfigFormat::Json).ok);
    const auto *current = reloader.Current();
    ASSERT_NE(current, nullptr);
    const auto port = cfgx::GetNode(*current, "svc.port");
    ASSERT_TRUE(port.ok) << port.error;
    EXPECT_EQ(port.value->AsInt(-1), 1000);

    const auto &trail = reloader.AuditTrail();
    ASSERT_GE(trail.size(), 3U);

    bool has_rollback = false;
    bool has_restore = false;
    for (const auto &entry : trail)
    {
        if (entry.action == "reload_rollback")
        {
            has_rollback = true;
            EXPECT_TRUE(entry.rolled_back);
        }
        if (entry.action == "restore_snapshot")
        {
            has_restore = true;
        }
    }
    EXPECT_TRUE(has_rollback);
    EXPECT_TRUE(has_restore);

    reloader.ClearAuditTrail();
    EXPECT_TRUE(reloader.AuditTrail().empty());

    std::error_code ec;
    std::filesystem::remove(file, ec);
    std::filesystem::remove(snapshot_file, ec);
}

TEST(CfgxParserAdapterTests, RegistryAndActivationApisWork)
{
    ParserAdapterScope scope;

    auto alpha = MakeFailingAdapter("Alpha");
    auto beta = MakeFailingAdapter("beta");

    ASSERT_TRUE(cfgx::RegisterParserAdapter(std::move(alpha)).ok);
    ASSERT_TRUE(cfgx::RegisterParserAdapter(std::move(beta)).ok);

    EXPECT_TRUE(cfgx::HasParserAdapter("ALPHA"));
    EXPECT_TRUE(cfgx::HasParserAdapter("beta"));

    const auto listed = cfgx::ListParserAdapters();
    EXPECT_EQ(listed.size(), 2U);
    EXPECT_TRUE(std::find(listed.begin(), listed.end(), "Alpha") != listed.end());
    EXPECT_TRUE(std::find(listed.begin(), listed.end(), "beta") != listed.end());

    ASSERT_TRUE(cfgx::SetActiveParserAdapter("alpha").ok);
    EXPECT_EQ(cfgx::GetActiveParserAdapter(), "Alpha");

    const auto missing_active = cfgx::SetActiveParserAdapter("missing");
    ASSERT_FALSE(missing_active.ok);

    ASSERT_TRUE(cfgx::UnregisterParserAdapter("alpha").ok);
    EXPECT_EQ(cfgx::GetActiveParserAdapter(), "");
}

TEST(CfgxParserAdapterTests, LoadUsesActiveAdapterWhenParseSucceeds)
{
    ParserAdapterScope scope;

    bool parse_called = false;
    cfgx::ParserAdapter adapter;
    adapter.name = "MockLoad";
    adapter.parse = [&](std::string_view text, cfgx::ConfigFormat format)
    {
        parse_called = true;
        EXPECT_EQ(format, cfgx::ConfigFormat::Json);
        EXPECT_FALSE(text.empty());

        cfgx::Node root = cfgx::Node::MakeObject();
        EXPECT_TRUE(cfgx::SetNode(root, "meta.via", cfgx::Node("adapter")).ok);

        cfgx::Result<cfgx::Node> out;
        out.ok = true;
        out.value = std::move(root);
        return out;
    };
    adapter.dump = [](const cfgx::Node &, cfgx::ConfigFormat, int)
    {
        cfgx::Result<std::string> out;
        out.ok = false;
        out.error = "unused";
        return out;
    };

    ASSERT_TRUE(cfgx::RegisterParserAdapter(std::move(adapter), true).ok);

    const auto file = TestTempPath("cfgx_adapter_load.json");
    {
        std::ofstream out(file.string(), std::ios::trunc);
        out << R"({"svc":{"port":1000}})";
    }

    const auto load = cfgx::LoadFromFile(file.string());
    ASSERT_TRUE(load.ok) << load.error;
    EXPECT_TRUE(parse_called);

    const auto via = cfgx::GetNode(load.value, "meta.via");
    ASSERT_TRUE(via.ok) << via.error;
    EXPECT_EQ(via.value->AsString(), "adapter");

    std::error_code ec;
    std::filesystem::remove(file, ec);
}

TEST(CfgxParserAdapterTests, SaveUsesActiveAdapterWhenDumpSucceeds)
{
    ParserAdapterScope scope;

    bool dump_called = false;
    cfgx::ParserAdapter adapter;
    adapter.name = "MockSave";
    adapter.parse = [](std::string_view, cfgx::ConfigFormat)
    {
        cfgx::Result<cfgx::Node> out;
        out.ok = false;
        out.error = "unused";
        return out;
    };
    adapter.dump = [&](const cfgx::Node &, cfgx::ConfigFormat format, int)
    {
        dump_called = true;
        EXPECT_EQ(format, cfgx::ConfigFormat::Json);
        cfgx::Result<std::string> out;
        out.ok = true;
        out.value = R"({"from":"adapter"})";
        return out;
    };

    ASSERT_TRUE(cfgx::RegisterParserAdapter(std::move(adapter), true).ok);

    const auto file = TestTempPath("cfgx_adapter_save.json");
    cfgx::Node root = cfgx::Node::MakeObject();
    ASSERT_TRUE(cfgx::SetNode(root, "demo.value", cfgx::Node("builtin")).ok);

    const auto save = cfgx::SaveToFile(root, file.string());
    ASSERT_TRUE(save.ok) << save.error;
    EXPECT_TRUE(dump_called);

    std::ifstream in(file.string(), std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    EXPECT_EQ(text, R"({"from":"adapter"})");

    std::error_code ec;
    std::filesystem::remove(file, ec);
}

TEST(CfgxParserAdapterTests, LoadFallsBackToBuiltinWhenAdapterParseFails)
{
    ParserAdapterScope scope;

    bool parse_called = false;
    cfgx::ParserAdapter adapter = MakeFailingAdapter("FailingLoad");
    adapter.parse = [&](std::string_view, cfgx::ConfigFormat)
    {
        parse_called = true;
        cfgx::Result<cfgx::Node> out;
        out.ok = false;
        out.error = "expected parse failure";
        return out;
    };

    ASSERT_TRUE(cfgx::RegisterParserAdapter(std::move(adapter), true).ok);

    const auto file = TestTempPath("cfgx_adapter_fallback_load.json");
    {
        std::ofstream out(file.string(), std::ios::trunc);
        out << R"({"svc":{"port":1234}})";
    }

    const auto load = cfgx::LoadFromFile(file.string());
    ASSERT_TRUE(load.ok) << load.error;
    EXPECT_TRUE(parse_called);

    const auto port = cfgx::GetNode(load.value, "svc.port");
    ASSERT_TRUE(port.ok) << port.error;
    EXPECT_EQ(port.value->AsInt(-1), 1234);

    std::error_code ec;
    std::filesystem::remove(file, ec);
}

TEST(CfgxParserAdapterTests, SaveFallsBackToBuiltinWhenAdapterDumpFails)
{
    ParserAdapterScope scope;

    bool dump_called = false;
    cfgx::ParserAdapter adapter = MakeFailingAdapter("FailingSave");
    adapter.dump = [&](const cfgx::Node &, cfgx::ConfigFormat, int)
    {
        dump_called = true;
        cfgx::Result<std::string> out;
        out.ok = false;
        out.error = "expected dump failure";
        return out;
    };

    ASSERT_TRUE(cfgx::RegisterParserAdapter(std::move(adapter), true).ok);

    const auto file = TestTempPath("cfgx_adapter_fallback_save.json");
    cfgx::Node root = cfgx::Node::MakeObject();
    ASSERT_TRUE(cfgx::SetNode(root, "demo.value", cfgx::Node("ok")).ok);

    const auto save = cfgx::SaveToFile(root, file.string());
    ASSERT_TRUE(save.ok) << save.error;
    EXPECT_TRUE(dump_called);

    const auto load = cfgx::LoadFromFile(file.string(), cfgx::ConfigFormat::Json);
    ASSERT_TRUE(load.ok) << load.error;
    const auto value = cfgx::GetNode(load.value, "demo.value");
    ASSERT_TRUE(value.ok) << value.error;
    EXPECT_EQ(value.value->AsString(), "ok");

    std::error_code ec;
    std::filesystem::remove(file, ec);
}

TEST(CfgxTomlTests, DetectFormatSupportsTomlExtension)
{
    EXPECT_EQ(cfgx::DetectFormatFromPath("settings.toml"), cfgx::ConfigFormat::Toml);
}

TEST(CfgxFormatTests, UnsupportedExplicitFormatReportsStableError)
{
    ParserAdapterScope adapters;
    const auto file = TestTempPath("cfgx_unsupported_format.demo");
    {
        std::ofstream out(file.string(), std::ios::trunc);
        out << "{}";
    }

    const auto loaded = cfgx::LoadFromFile(file.string(), static_cast<cfgx::ConfigFormat>(99));
    ASSERT_FALSE(loaded.ok);
    EXPECT_NE(loaded.error.find("unsupported config format"), std::string::npos);

    std::error_code ec;
    std::filesystem::remove(file, ec);
}

TEST(CfgxTomlTests, LoadAndSaveTomlRoundTrip)
{
    const auto file = TestTempPath("cfgx_toml_roundtrip.toml");

    {
        std::ofstream out(file.string(), std::ios::trunc);
        out << R"(
enabled = true
port = 8080

[service]
name = "demo"
ratio = 0.75
)";
    }

    auto loaded = cfgx::LoadFromFile(file.string());
    ASSERT_TRUE(loaded.ok) << loaded.error;

    const auto enabled = cfgx::GetNode(loaded.value, "enabled");
    ASSERT_TRUE(enabled.ok) << enabled.error;
    EXPECT_TRUE(enabled.value->AsBool());

    const auto port = cfgx::GetNode(loaded.value, "port");
    ASSERT_TRUE(port.ok) << port.error;
    EXPECT_EQ(port.value->AsInt(-1), 8080);

    const auto name = cfgx::GetNode(loaded.value, "service.name");
    ASSERT_TRUE(name.ok) << name.error;
    EXPECT_EQ(name.value->AsString(), "demo");

    const auto ratio = cfgx::GetNode(loaded.value, "service.ratio");
    ASSERT_TRUE(ratio.ok) << ratio.error;
    EXPECT_DOUBLE_EQ(ratio.value->AsDouble(-1.0), 0.75);

    ASSERT_TRUE(cfgx::SetNode(loaded.value, "service.name", cfgx::Node("demo-v2")).ok);
    const auto saved = cfgx::SaveToFile(loaded.value, file.string(), cfgx::ConfigFormat::Toml);
    ASSERT_TRUE(saved.ok) << saved.error;

    const auto reloaded = cfgx::LoadFromFile(file.string(), cfgx::ConfigFormat::Toml);
    ASSERT_TRUE(reloaded.ok) << reloaded.error;

    const auto updated_name = cfgx::GetNode(reloaded.value, "service.name");
    ASSERT_TRUE(updated_name.ok) << updated_name.error;
    EXPECT_EQ(updated_name.value->AsString(), "demo-v2");

    std::error_code ec;
    std::filesystem::remove(file, ec);
}

TEST(CfgxRemoteTests, LoadFromRemoteRequiresFetcher)
{
    RemoteFetcherScope scope;

    EXPECT_FALSE(cfgx::HasRemoteFetcher());
    const auto loaded = cfgx::LoadFromRemote("https://config.test/app.json");
    ASSERT_FALSE(loaded.ok);
    EXPECT_NE(loaded.error.find("remote fetcher"), std::string::npos);
}

TEST(CfgxRemoteTests, LoadFromRemoteParsesJsonWithFetcher)
{
    RemoteFetcherScope scope;

    std::string seen_url;
    cfgx::SetRemoteFetcher([&](const cfgx::RemoteFetchRequest &request)
                           {
                               seen_url = request.url;
                               cfgx::Result<cfgx::RemoteFetchResponse> out;
                               out.ok = true;
                               out.value.body = R"({"svc":{"host":"127.0.0.1","port":8080}})";
                               return out; });

    ASSERT_TRUE(cfgx::HasRemoteFetcher());
    const auto loaded = cfgx::LoadFromRemote("https://config.test/app.json?version=1");
    ASSERT_TRUE(loaded.ok) << loaded.error;
    EXPECT_EQ(seen_url, "https://config.test/app.json?version=1");

    const auto host = cfgx::GetNode(loaded.value, "svc.host");
    ASSERT_TRUE(host.ok) << host.error;
    EXPECT_EQ(host.value->AsString(), "127.0.0.1");

    const auto port = cfgx::GetNode(loaded.value, "svc.port");
    ASSERT_TRUE(port.ok) << port.error;
    EXPECT_EQ(port.value->AsInt(-1), 8080);
}

TEST(CfgxRemoteTests, LoadFromRemoteDetectsTomlByUrl)
{
    RemoteFetcherScope scope;

    cfgx::SetRemoteFetcher([](const cfgx::RemoteFetchRequest &)
                           {
                               cfgx::Result<cfgx::RemoteFetchResponse> out;
                               out.ok = true;
                               out.value.body = "enabled = true\n[service]\nname = \"demo\"\n";
                               return out; });

    const auto loaded = cfgx::LoadFromRemote("https://config.test/app.toml#head");
    ASSERT_TRUE(loaded.ok) << loaded.error;

    const auto enabled = cfgx::GetNode(loaded.value, "enabled");
    ASSERT_TRUE(enabled.ok) << enabled.error;
    EXPECT_TRUE(enabled.value->AsBool(false));

    const auto name = cfgx::GetNode(loaded.value, "service.name");
    ASSERT_TRUE(name.ok) << name.error;
    EXPECT_EQ(name.value->AsString(), "demo");
}

TEST(CfgxRemoteTests, PollReloaderReloadNowAppliesRemoteLayer)
{
    RemoteFetcherScope scope;

    const auto file = TestTempPath("cfgx_remote_reload_now.json");
    {
        std::ofstream out(file.string(), std::ios::trunc);
        out << R"({"svc":{"host":"127.0.0.1","port":8080}})";
    }

    cfgx::SetRemoteFetcher([](const cfgx::RemoteFetchRequest &)
                           {
                               cfgx::Result<cfgx::RemoteFetchResponse> out;
                               out.ok = true;
                               out.value.body = R"({"svc":{"port":9001}})";
                               return out; });

    cfgx::PollReloader reloader(file.string());
    cfgx::ReloadOptions options;
    options.remote_url = "https://config.test/override.json";
    options.allow_remote_failure = false;
    reloader.SetOptions(options);

    const auto reload = reloader.ReloadNow();
    ASSERT_TRUE(reload.ok) << reload.error;
    EXPECT_TRUE(reload.value.attempted);

    const auto *current = reloader.Current();
    ASSERT_NE(current, nullptr);
    const auto port = cfgx::GetNode(*current, "svc.port");
    ASSERT_TRUE(port.ok) << port.error;
    EXPECT_EQ(port.value->AsInt(-1), 9001);

    bool has_remote_source = false;
    for (const auto &entry : reload.value.source_trace)
    {
        if (entry.path == "svc.port")
        {
            has_remote_source = true;
            EXPECT_EQ(entry.layer, cfgx::SourceLayer::Remote);
        }
    }
    EXPECT_TRUE(has_remote_source);

    std::error_code ec;
    std::filesystem::remove(file, ec);
}

TEST(CfgxRemoteTests, PollReloaderTickPollsRemoteByInterval)
{
    RemoteFetcherScope scope;

    const auto file = TestTempPath("cfgx_remote_tick_poll.json");
    {
        std::ofstream out(file.string(), std::ios::trunc);
        out << R"({"svc":{"port":1000}})";
    }

    int call_count = 0;
    cfgx::SetRemoteFetcher([&](const cfgx::RemoteFetchRequest &)
                           {
                               ++call_count;
                               cfgx::Result<cfgx::RemoteFetchResponse> out;
                               out.ok = true;
                               out.value.body = (call_count == 1)
                                                    ? R"({"svc":{"port":1001}})"
                                                    : R"({"svc":{"port":1002}})";
                               return out; });

    cfgx::PollReloader reloader(file.string());
    cfgx::ReloadOptions options;
    options.remote_url = "https://config.test/app.json";
    options.remote_poll_interval_ms = 100;
    options.debounce_ms = 0;
    reloader.SetOptions(options);

    const auto init = reloader.Tick(10);
    ASSERT_TRUE(init.ok) << init.error;
    ASSERT_TRUE(init.value.attempted);
    ASSERT_EQ(call_count, 1);

    const auto idle = reloader.Tick(50);
    ASSERT_TRUE(idle.ok) << idle.error;
    EXPECT_FALSE(idle.value.attempted);

    const auto polled = reloader.Tick(120);
    ASSERT_TRUE(polled.ok) << polled.error;
    EXPECT_TRUE(polled.value.attempted);
    EXPECT_EQ(call_count, 2);

    const auto *current = reloader.Current();
    ASSERT_NE(current, nullptr);
    const auto port = cfgx::GetNode(*current, "svc.port");
    ASSERT_TRUE(port.ok) << port.error;
    EXPECT_EQ(port.value->AsInt(-1), 1002);

    std::error_code ec;
    std::filesystem::remove(file, ec);
}

TEST(CfgxEncryptedTests, SaveAndLoadEncryptedJson)
{
    const auto file = TestTempPath("cfgx_secure.json");

    cfgx::Node root = cfgx::Node::MakeObject();
    ASSERT_TRUE(cfgx::SetNode(root, "svc.token", cfgx::Node("top-secret")).ok);
    ASSERT_TRUE(cfgx::SetNode(root, "svc.port", cfgx::Node(std::int64_t(8443))).ok);

    const auto saved = cfgx::SaveEncryptedToFile(root, file.string(), "k-demo-123", cfgx::ConfigFormat::Json, 2);
    ASSERT_TRUE(saved.ok) << saved.error;

    std::ifstream in(file.string(), std::ios::binary);
    const std::string raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    EXPECT_EQ(raw.find("top-secret"), std::string::npos);

    const auto loaded = cfgx::LoadEncryptedFromFile(file.string(), "k-demo-123", cfgx::ConfigFormat::Json);
    ASSERT_TRUE(loaded.ok) << loaded.error;

    const auto token = cfgx::GetNode(loaded.value, "svc.token");
    ASSERT_TRUE(token.ok) << token.error;
    EXPECT_EQ(token.value->AsString(), "top-secret");

    const auto port = cfgx::GetNode(loaded.value, "svc.port");
    ASSERT_TRUE(port.ok) << port.error;
    EXPECT_EQ(port.value->AsInt(-1), 8443);

    std::error_code ec;
    std::filesystem::remove(file, ec);
}

TEST(CfgxEncryptedTests, WrongKeyFailsToLoad)
{
    const auto file = TestTempPath("cfgx_secure_wrong_key.json");

    cfgx::Node root = cfgx::Node::MakeObject();
    ASSERT_TRUE(cfgx::SetNode(root, "svc.name", cfgx::Node("demo")).ok);
    ASSERT_TRUE(cfgx::SaveEncryptedToFile(root, file.string(), "key-A", cfgx::ConfigFormat::Json, 2).ok);

    const auto loaded = cfgx::LoadEncryptedFromFile(file.string(), "key-B", cfgx::ConfigFormat::Json);
    ASSERT_FALSE(loaded.ok);

    std::error_code ec;
    std::filesystem::remove(file, ec);
}
