#include "schemax.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace schemax
{
namespace
{
constexpr std::string_view kSupportedKeys[] = {
    "type",    "required", "properties", "items",     "minimum",
    "maximum", "enum",     "minLength",  "maxLength", "additionalProperties"};

bool is_supported_key(std::string_view key)
{
    return std::find(std::begin(kSupportedKeys), std::end(kSupportedKeys), key) != std::end(kSupportedKeys);
}

cfgx::Result<Schema> compile_error(std::string message)
{
    return cfgx::Result<Schema>{false, Schema{}, std::move(message)};
}

bool is_integer_number(const cfgx::Node& node)
{
    return node.Kind() == cfgx::NodeKind::Integer ||
           (node.Kind() == cfgx::NodeKind::Double && std::floor(node.AsDouble()) == node.AsDouble());
}

bool is_number(const cfgx::Node& node)
{
    return node.Kind() == cfgx::NodeKind::Integer || node.Kind() == cfgx::NodeKind::Double;
}

double as_number(const cfgx::Node& node)
{
    return node.Kind() == cfgx::NodeKind::Integer ? static_cast<double>(node.AsInt()) : node.AsDouble();
}

std::string append_path(std::string_view parent, std::string_view child)
{
    if (parent.empty())
    {
        return std::string(child);
    }
    return std::string(parent) + "." + std::string(child);
}

std::string append_index(std::string_view parent, std::size_t index)
{
    std::ostringstream oss;
    oss << parent << "[" << index << "]";
    return oss.str();
}

bool node_equals(const cfgx::Node& lhs, const cfgx::Node& rhs)
{
    if (lhs.Kind() != rhs.Kind())
    {
        if (is_number(lhs) && is_number(rhs))
        {
            return as_number(lhs) == as_number(rhs);
        }
        return false;
    }

    switch (lhs.Kind())
    {
    case cfgx::NodeKind::Null:
        return true;
    case cfgx::NodeKind::Bool:
        return lhs.AsBool() == rhs.AsBool();
    case cfgx::NodeKind::Integer:
        return lhs.AsInt() == rhs.AsInt();
    case cfgx::NodeKind::Double:
        return lhs.AsDouble() == rhs.AsDouble();
    case cfgx::NodeKind::String:
        return lhs.AsString() == rhs.AsString();
    case cfgx::NodeKind::Object:
    {
        const auto* lhs_obj = lhs.TryObject();
        const auto* rhs_obj = rhs.TryObject();
        if (lhs_obj == nullptr || rhs_obj == nullptr || lhs_obj->size() != rhs_obj->size())
        {
            return false;
        }
        for (const auto& entry : *lhs_obj)
        {
            const auto* rhs_child = rhs.Get(entry.first);
            if (rhs_child == nullptr || !node_equals(entry.second, *rhs_child))
            {
                return false;
            }
        }
        return true;
    }
    case cfgx::NodeKind::Array:
    {
        const auto* lhs_arr = lhs.TryArray();
        const auto* rhs_arr = rhs.TryArray();
        if (lhs_arr == nullptr || rhs_arr == nullptr || lhs_arr->size() != rhs_arr->size())
        {
            return false;
        }
        for (std::size_t i = 0; i < lhs_arr->size(); ++i)
        {
            if (!node_equals((*lhs_arr)[i], (*rhs_arr)[i]))
            {
                return false;
            }
        }
        return true;
    }
    }
    return false;
}

bool type_matches(const cfgx::Node& document, std::string_view expected)
{
    if (expected == "null")
    {
        return document.Kind() == cfgx::NodeKind::Null;
    }
    if (expected == "bool" || expected == "boolean")
    {
        return document.Kind() == cfgx::NodeKind::Bool;
    }
    if (expected == "int" || expected == "integer")
    {
        return is_integer_number(document);
    }
    if (expected == "double" || expected == "number")
    {
        return is_number(document);
    }
    if (expected == "string")
    {
        return document.Kind() == cfgx::NodeKind::String;
    }
    if (expected == "object")
    {
        return document.Kind() == cfgx::NodeKind::Object;
    }
    if (expected == "array")
    {
        return document.Kind() == cfgx::NodeKind::Array;
    }
    return false;
}

bool is_known_type(std::string_view type)
{
    static constexpr std::string_view kTypes[] = {"null",   "bool",   "boolean", "int",    "integer",
                                                  "double", "number", "string",  "object", "array"};
    return std::find(std::begin(kTypes), std::end(kTypes), type) != std::end(kTypes);
}

bool push_issue(std::vector<Issue>* issues, const Options& options, Issue issue)
{
    issues->push_back(std::move(issue));
    return options.fail_fast;
}

cfgx::Result<Schema> validate_schema_node(const cfgx::Node& schema, std::string_view path)
{
    if (!schema.IsObject())
    {
        return compile_error("schema node must be an object at " + std::string(path.empty() ? "$" : path));
    }

    const auto* obj = schema.TryObject();
    for (const auto& entry : *obj)
    {
        if (!is_supported_key(entry.first))
        {
            return compile_error("unsupported schema key '" + entry.first + "' at " +
                                 std::string(path.empty() ? "$" : path));
        }
    }

    if (const auto* type = schema.Get("type"); type != nullptr)
    {
        if (type->Kind() != cfgx::NodeKind::String || !is_known_type(type->AsString()))
        {
            return compile_error("schema type must be a supported string at " + std::string(path.empty() ? "$" : path));
        }
    }

    if (const auto* required = schema.Get("required"); required != nullptr)
    {
        const auto* arr = required->TryArray();
        if (arr == nullptr)
        {
            return compile_error("schema required must be an array at " + std::string(path.empty() ? "$" : path));
        }
        for (const auto& item : *arr)
        {
            if (item.Kind() != cfgx::NodeKind::String)
            {
                return compile_error("schema required entries must be strings at " +
                                     std::string(path.empty() ? "$" : path));
            }
        }
    }

    if (const auto* properties = schema.Get("properties"); properties != nullptr)
    {
        const auto* prop_obj = properties->TryObject();
        if (prop_obj == nullptr)
        {
            return compile_error("schema properties must be an object at " + std::string(path.empty() ? "$" : path));
        }
        for (const auto& property : *prop_obj)
        {
            auto nested = validate_schema_node(property.second, append_path(path, property.first));
            if (!nested.ok)
            {
                return nested;
            }
        }
    }

    if (const auto* items = schema.Get("items"); items != nullptr)
    {
        auto nested = validate_schema_node(*items, append_path(path, "items"));
        if (!nested.ok)
        {
            return nested;
        }
    }

    for (std::string_view numeric_key : {"minimum", "maximum"})
    {
        if (const auto* value = schema.Get(numeric_key); value != nullptr && !is_number(*value))
        {
            return compile_error("schema " + std::string(numeric_key) + " must be numeric at " +
                                 std::string(path.empty() ? "$" : path));
        }
    }

    for (std::string_view length_key : {"minLength", "maxLength"})
    {
        if (const auto* value = schema.Get(length_key); value != nullptr && value->Kind() != cfgx::NodeKind::Integer)
        {
            return compile_error("schema " + std::string(length_key) + " must be an integer at " +
                                 std::string(path.empty() ? "$" : path));
        }
    }

    if (const auto* enum_values = schema.Get("enum");
        enum_values != nullptr && enum_values->Kind() != cfgx::NodeKind::Array)
    {
        return compile_error("schema enum must be an array at " + std::string(path.empty() ? "$" : path));
    }

    if (const auto* additional = schema.Get("additionalProperties");
        additional != nullptr && additional->Kind() != cfgx::NodeKind::Bool)
    {
        return compile_error("schema additionalProperties must be boolean at " +
                             std::string(path.empty() ? "$" : path));
    }

    return cfgx::Result<Schema>{true, Schema(schema), ""};
}

void validate_node(const cfgx::Node& document, const cfgx::Node& schema, std::string_view path, const Options& options,
                   std::vector<Issue>* issues)
{
    const std::string location = path.empty() ? "$" : std::string(path);

    if (const auto* type = schema.Get("type"); type != nullptr && !type_matches(document, type->AsString()))
    {
        if (push_issue(
                issues, options,
                Issue{location, "type",
                      "expected type '" + type->AsString() + "', got '" + cfgx::ToString(document.Kind()) + "'"}))
        {
            return;
        }
    }

    if (const auto* required = schema.Get("required"); required != nullptr && document.IsObject())
    {
        const auto* arr = required->TryArray();
        for (const auto& item : *arr)
        {
            const std::string key = item.AsString();
            if (document.Get(key) == nullptr &&
                push_issue(issues, options, Issue{append_path(path, key), "required", "required property is missing"}))
            {
                return;
            }
        }
    }

    if (const auto* properties = schema.Get("properties"); properties != nullptr && document.IsObject())
    {
        const auto* prop_obj = properties->TryObject();
        for (const auto& property : *prop_obj)
        {
            const auto* child = document.Get(property.first);
            if (child != nullptr)
            {
                validate_node(*child, property.second, append_path(path, property.first), options, issues);
                if (options.fail_fast && !issues->empty())
                {
                    return;
                }
            }
        }

        if (const auto* additional = schema.Get("additionalProperties");
            additional != nullptr && additional->Kind() == cfgx::NodeKind::Bool && !additional->AsBool())
        {
            const auto* doc_obj = document.TryObject();
            for (const auto& entry : *doc_obj)
            {
                if (properties->Get(entry.first) == nullptr &&
                    push_issue(issues, options,
                               Issue{append_path(path, entry.first), "additionalProperties",
                                     "additional property is not allowed"}))
                {
                    return;
                }
            }
        }
    }

    if (const auto* items = schema.Get("items"); items != nullptr && document.IsArray())
    {
        const auto* arr = document.TryArray();
        for (std::size_t i = 0; i < arr->size(); ++i)
        {
            validate_node((*arr)[i], *items, append_index(path, i), options, issues);
            if (options.fail_fast && !issues->empty())
            {
                return;
            }
        }
    }

    if (is_number(document))
    {
        const double actual = as_number(document);
        if (const auto* minimum = schema.Get("minimum"); minimum != nullptr && actual < as_number(*minimum))
        {
            if (push_issue(issues, options, Issue{location, "minimum", "numeric value is below minimum"}))
            {
                return;
            }
        }
        if (const auto* maximum = schema.Get("maximum"); maximum != nullptr && actual > as_number(*maximum))
        {
            if (push_issue(issues, options, Issue{location, "maximum", "numeric value is above maximum"}))
            {
                return;
            }
        }
    }

    if (document.Kind() == cfgx::NodeKind::String)
    {
        const std::size_t len = document.AsString().size();
        if (const auto* min_len = schema.Get("minLength");
            min_len != nullptr && len < static_cast<std::size_t>(min_len->AsInt()))
        {
            if (push_issue(issues, options, Issue{location, "minLength", "string length is below minimum"}))
            {
                return;
            }
        }
        if (const auto* max_len = schema.Get("maxLength");
            max_len != nullptr && len > static_cast<std::size_t>(max_len->AsInt()))
        {
            if (push_issue(issues, options, Issue{location, "maxLength", "string length is above maximum"}))
            {
                return;
            }
        }
    }

    if (const auto* enum_values = schema.Get("enum"); enum_values != nullptr)
    {
        const auto* arr = enum_values->TryArray();
        const bool found = std::any_of(arr->begin(), arr->end(), [&document](const cfgx::Node& allowed)
                                       { return node_equals(document, allowed); });
        if (!found)
        {
            (void)push_issue(issues, options, Issue{location, "enum", "value is not in enum set"});
        }
    }
}
} // namespace

Schema::Schema(cfgx::Node root) : root_(std::move(root)) {}

const cfgx::Node& Schema::Root() const noexcept
{
    return root_;
}

cfgx::Result<Schema> Compile(const cfgx::Node& schema_root)
{
    return validate_schema_node(schema_root, "");
}

std::vector<Issue> Validate(const cfgx::Node& document, const Schema& schema, const Options& options)
{
    std::vector<Issue> issues;
    validate_node(document, schema.Root(), "", options, &issues);
    return issues;
}

std::vector<cfgx::ValidationIssue> ToCfgxIssues(const std::vector<Issue>& issues)
{
    std::vector<cfgx::ValidationIssue> out;
    out.reserve(issues.size());
    for (const auto& issue : issues)
    {
        out.push_back({issue.path, "[" + issue.code + "] " + issue.message});
    }
    return out;
}

} // namespace schemax
