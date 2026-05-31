#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "cfgx.h"
#include "schemax.h"
#include "tuix.h"

namespace
{
void collect_paths(const cfgx::Node& node, std::string prefix, std::vector<std::string>* out)
{
    if (node.IsObject())
    {
        const auto* obj = node.TryObject();
        for (const auto& entry : *obj)
        {
            collect_paths(entry.second, prefix.empty() ? entry.first : prefix + "." + entry.first, out);
        }
        return;
    }
    if (node.IsArray())
    {
        const auto* arr = node.TryArray();
        for (std::size_t i = 0; i < arr->size(); ++i)
        {
            collect_paths((*arr)[i], prefix + "[" + std::to_string(i) + "]", out);
        }
        return;
    }
    out->push_back(prefix.empty() ? "$" : prefix);
}

std::vector<std::string> issue_lines(const std::vector<schemax::Issue>& issues)
{
    std::vector<std::string> out;
    if (issues.empty())
    {
        out.push_back("schema: ok");
        return out;
    }
    for (const auto& issue : issues)
    {
        out.push_back(issue.path + " [" + issue.code + "] " + issue.message);
    }
    return out;
}
} // namespace

int main(int argc, const char* const argv[])
{
    cfgx::Node config = cfgx::Node::MakeObject();
    std::vector<schemax::Issue> schema_issues;
    std::string status = "no config supplied";

    if (argc >= 2)
    {
        const auto loaded = cfgx::LoadFromFile(argv[1]);
        if (!loaded.ok)
        {
            status = "load failed: " + loaded.error;
        }
        else
        {
            config = loaded.value;
            status = std::string("loaded ") + argv[1];
        }
    }

    if (argc >= 3)
    {
        const auto loaded_schema = cfgx::LoadFromFile(argv[2]);
        if (!loaded_schema.ok)
        {
            schema_issues.push_back({"$", "schema_load", loaded_schema.error});
        }
        else
        {
            const auto compiled = schemax::Compile(loaded_schema.value);
            if (!compiled.ok)
            {
                schema_issues.push_back({"$", "schema_compile", compiled.error});
            }
            else
            {
                schema_issues = schemax::Validate(config, compiled.value);
            }
        }
    }

    std::vector<std::string> paths;
    collect_paths(config, "", &paths);
    if (paths.empty())
    {
        paths.push_back("$");
    }

    tuix::Terminal terminal(std::cout, true);
    tuix::Application app(&terminal);

    auto root = std::make_shared<tuix::VerticalLayout>();
    root->SetPadding({1, 1, 1, 1});
    root->SetGap(1);
    root->SetFlexWeights({1, 3, 3});

    auto title = std::make_shared<tuix::Label>("ToolX config inspector");
    auto main_row = std::make_shared<tuix::HorizontalLayout>();
    main_row->SetGap(1);
    main_row->SetFlexWeights({1, 2});

    auto path_panel = std::make_shared<tuix::Panel>("paths");
    auto path_list = std::make_shared<tuix::ListView>(paths);
    path_panel->AddChild(path_list);

    auto issue_panel = std::make_shared<tuix::Panel>("schema");
    auto issue_list = std::make_shared<tuix::ListView>(issue_lines(schema_issues));
    issue_panel->AddChild(issue_list);

    main_row->AddChild(path_panel);
    main_row->AddChild(issue_panel);

    auto status_input = std::make_shared<tuix::TextInput>(status);
    status_input->SetPlaceholder("status");

    root->AddChild(title);
    root->AddChild(main_row);
    root->AddChild(status_input);

    std::istringstream scripted("\t\x1B[B\x1B");
    app.SetRoot(root);
    app.SetInputSource(tuix::CreateStreamInputSource(scripted));
    app.Run(4, 0);
    return schema_issues.empty() ? 0 : 4;
}
