#include "argtool.h"

#include <array>
#include <algorithm>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <tuple>

namespace argtool
{
    // 实用工具函数放在匿名命名空间，文件内部可见。
    // 这些函数负责字符串规范化、类型解析与帮助文本生成等。
    namespace
    {

        constexpr int kExitOk = 0;
        constexpr int kExitParseError = 2;

        // 返回小写的拷贝（不修改输入）。用于不区分大小写的比较场景。
        std::string ToLowerCopy(std::string_view text)
        {
            std::string out(text);
            std::transform(out.begin(), out.end(), out.begin(), [](unsigned char ch)
                           { return static_cast<char>(std::tolower(ch)); });
            return out;
        }

        // 忽略大小写比较两个字符串。
        bool EqualsIgnoreCase(std::string_view lhs, std::string_view rhs)
        {
            return ToLowerCopy(lhs) == ToLowerCopy(rhs);
        }

        std::string ValueTypeName(ValueType type)
        {
            switch (type)
            {
            case ValueType::String:
                return "string";
            case ValueType::Int:
                return "int";
            case ValueType::Double:
                return "double";
            case ValueType::Bool:
                return "bool";
            }
            return "unknown";
        }

        std::string RepeatModeName(RepeatMode mode)
        {
            switch (mode)
            {
            case RepeatMode::Override:
                return "override";
            case RepeatMode::Append:
                return "append";
            case RepeatMode::Count:
                return "count";
            }
            return "override";
        }

        std::string BoolFlagModeName(BoolFlagMode mode)
        {
            switch (mode)
            {
            case BoolFlagMode::Switch:
                return "switch";
            case BoolFlagMode::Count:
                return "count";
            case BoolFlagMode::Toggle:
                return "toggle";
            }
            return "switch";
        }

        // 尝试把字符串解析为 int，成功返回 true 并写入 out_value。
        bool TryParseInt(std::string_view value, int *out_value)
        {
            int parsed = 0;
            const auto *begin = value.data();
            const auto *end = value.data() + value.size();
            const auto result = std::from_chars(begin, end, parsed);
            if (result.ec != std::errc() || result.ptr != end)
            {
                return false;
            }
            *out_value = parsed;
            return true;
        }

        // 尝试把字符串解析为 double，成功返回 true 并写入 out_value。
        bool TryParseDouble(std::string_view value, double *out_value)
        {
            std::string copy(value);
            char *parse_end = nullptr;
            const double parsed = std::strtod(copy.c_str(), &parse_end);
            if (parse_end == nullptr || *parse_end != '\0')
            {
                return false;
            }
            *out_value = parsed;
            return true;
        }

        // 尝试把字符串解析为布尔值，支持 true/false/1/0/yes/no/on/off。
        bool TryParseBool(std::string_view value, bool *out_value)
        {
            const std::string lower = ToLowerCopy(value);
            if (lower == "1" || lower == "true" || lower == "yes" || lower == "on")
            {
                *out_value = true;
                return true;
            }
            if (lower == "0" || lower == "false" || lower == "no" || lower == "off")
            {
                *out_value = false;
                return true;
            }
            return false;
        }

        bool TryParseUnitMultiplier(std::string_view suffix, double *multiplier)
        {
            const std::string normalized = ToLowerCopy(suffix);
            if (normalized.empty())
            {
                *multiplier = 1.0;
                return true;
            }

            if (normalized == "ms")
            {
                *multiplier = 1.0;
                return true;
            }
            if (normalized == "s")
            {
                *multiplier = 1000.0;
                return true;
            }
            if (normalized == "m")
            {
                *multiplier = 60000.0;
                return true;
            }
            if (normalized == "h")
            {
                *multiplier = 3600000.0;
                return true;
            }

            if (normalized == "kb")
            {
                *multiplier = 1024.0;
                return true;
            }
            if (normalized == "mb")
            {
                *multiplier = 1024.0 * 1024.0;
                return true;
            }
            if (normalized == "gb")
            {
                *multiplier = 1024.0 * 1024.0 * 1024.0;
                return true;
            }

            return false;
        }

        bool TryParseScaledDouble(std::string_view value, double *out_value)
        {
            if (value.empty())
            {
                return false;
            }

            std::size_t split = value.size();
            while (split > 0)
            {
                const char ch = value[split - 1];
                if (std::isalpha(static_cast<unsigned char>(ch)) != 0)
                {
                    --split;
                    continue;
                }
                break;
            }

            const std::string number_part(value.substr(0, split));
            const std::string suffix_part(value.substr(split));
            if (number_part.empty())
            {
                return false;
            }

            double base = 0.0;
            if (!TryParseDouble(number_part, &base))
            {
                return false;
            }

            double multiplier = 1.0;
            if (!TryParseUnitMultiplier(suffix_part, &multiplier))
            {
                return false;
            }

            *out_value = base * multiplier;
            return true;
        }

        ConvertResult BuiltinStringConverter(std::string_view raw)
        {
            ConvertResult out;
            out.ok = true;
            out.value = std::string(raw);
            return out;
        }

        ConvertResult BuiltinIntConverter(std::string_view raw)
        {
            ConvertResult out;
            double scaled = 0.0;
            if (!TryParseScaledDouble(raw, &scaled))
            {
                out.ok = false;
                out.error = "Invalid int value: '" + std::string(raw) + "'.";
                return out;
            }

            const double rounded = std::round(scaled);
            if (std::fabs(rounded - scaled) > 1e-9)
            {
                out.ok = false;
                out.error = "Invalid int value (fractional after unit conversion): '" + std::string(raw) + "'.";
                return out;
            }

            if (rounded < static_cast<double>(std::numeric_limits<int>::min()) ||
                rounded > static_cast<double>(std::numeric_limits<int>::max()))
            {
                out.ok = false;
                out.error = "Int value out of range after unit conversion: '" + std::string(raw) + "'.";
                return out;
            }

            out.ok = true;
            out.value = std::to_string(static_cast<int>(rounded));
            return out;
        }

        ConvertResult BuiltinDoubleConverter(std::string_view raw)
        {
            ConvertResult out;
            double scaled = 0.0;
            if (!TryParseScaledDouble(raw, &scaled))
            {
                out.ok = false;
                out.error = "Invalid double value: '" + std::string(raw) + "'.";
                return out;
            }

            std::ostringstream oss;
            oss << scaled;
            out.ok = true;
            out.value = oss.str();
            return out;
        }

        ConvertResult BuiltinBoolConverter(std::string_view raw)
        {
            ConvertResult out;
            bool parsed = false;
            if (!TryParseBool(raw, &parsed))
            {
                out.ok = false;
                out.error = "Invalid bool value: '" + std::string(raw) + "'.";
                return out;
            }

            out.ok = true;
            out.value = parsed ? "true" : "false";
            return out;
        }

        // 将范围信息格式化为可读字符串："min=... , max=..."。
        std::string FormatRange(std::optional<double> min_value, std::optional<double> max_value)
        {
            if (!min_value.has_value() && !max_value.has_value())
            {
                return "-";
            }

            std::ostringstream oss;
            if (min_value.has_value())
            {
                oss << "min=" << *min_value;
            }
            if (min_value.has_value() && max_value.has_value())
            {
                oss << ", ";
            }
            if (max_value.has_value())
            {
                oss << "max=" << *max_value;
            }
            return oss.str();
        }

        // 将字符串列表用分隔符连接，返回连接后的结果。
        std::string JoinStrings(const std::vector<std::string> &values, std::string_view sep)
        {
            if (values.empty())
            {
                return "";
            }
            std::ostringstream oss;
            for (std::size_t i = 0; i < values.size(); ++i)
            {
                if (i > 0)
                {
                    oss << sep;
                }
                oss << values[i];
            }
            return oss.str();
        }

        std::string JsonEscape(std::string_view input)
        {
            std::string out;
            out.reserve(input.size() + 8);
            for (char ch : input)
            {
                switch (ch)
                {
                case '\\':
                    out += "\\\\";
                    break;
                case '"':
                    out += "\\\"";
                    break;
                case '\n':
                    out += "\\n";
                    break;
                case '\r':
                    out += "\\r";
                    break;
                case '\t':
                    out += "\\t";
                    break;
                default:
                    out.push_back(ch);
                    break;
                }
            }
            return out;
        }

        // 将长文本按宽度换行，返回行数组（用于 help 文本显示）。
        std::vector<std::string> WrapText(const std::string &text, std::size_t width)
        {
            if (text.empty())
            {
                return {""};
            }

            std::vector<std::string> lines;
            std::string remaining = text;
            while (!remaining.empty())
            {
                if (remaining.size() <= width)
                {
                    lines.push_back(remaining);
                    break;
                }

                std::size_t split = remaining.rfind(' ', width);
                if (split == std::string::npos || split == 0)
                {
                    split = width;
                }

                lines.push_back(remaining.substr(0, split));
                std::size_t next = split;
                while (next < remaining.size() && remaining[next] == ' ')
                {
                    ++next;
                }
                remaining = remaining.substr(next);
            }
            return lines;
        }

        // 在不区分大小写的情况下，判断 value 是否包含于 choices 中。
        bool ValidateChoiceIgnoreCase(const std::string &value, const std::vector<std::string> &choices)
        {
            if (choices.empty())
            {
                return true;
            }

            for (const auto &choice : choices)
            {
                if (EqualsIgnoreCase(value, choice))
                {
                    return true;
                }
            }
            return false;
        }

        // 在 choices 中找到与 value 忽略大小写匹配的项，返回原始 choices 中的项（保留大小写），找不到则返回 value。
        std::string NormalizeChoice(const std::string &value, const std::vector<std::string> &choices)
        {
            for (const auto &choice : choices)
            {
                if (EqualsIgnoreCase(value, choice))
                {
                    return choice;
                }
            }
            return value;
        }

        // 将 key 转为可作为 map key 的 std::string（目前为简单拷贝，可扩展为规范化）。
        std::string ToKey(std::string_view key)
        {
            return std::string(key);
        }

        // 将文本转换为大写标识符（非字母数字替换为下划线），用于生成 VALUE 名称等。
        std::string ToUpperIdentifier(std::string_view text)
        {
            std::string out;
            out.reserve(text.size());
            for (char ch : text)
            {
                if (std::isalnum(static_cast<unsigned char>(ch)) != 0)
                {
                    out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
                }
                else
                {
                    out.push_back('_');
                }
            }
            return out.empty() ? std::string("VALUE") : out;
        }

        // 去掉前导 '-' 或 '--'，规范化 long option 名称。
        std::string NormalizeLongName(std::string name)
        {
            while (name.rfind("--", 0) == 0)
            {
                name = name.substr(2);
            }
            while (!name.empty() && name.front() == '-')
            {
                name.erase(name.begin());
            }
            return name;
        }

        // 格式化一个选项的名称串，如 "-v, --verbose, --verb"，用于 help 输出。
        std::string FormatOptionNames(const std::string &long_name,
                                      char short_name,
                                      const std::vector<std::string> &long_aliases,
                                      const std::vector<char> &short_aliases)
        {
            std::vector<std::string> parts;
            if (short_name != '\0')
            {
                parts.push_back("-" + std::string(1, short_name));
            }
            for (char c : short_aliases)
            {
                parts.push_back("-" + std::string(1, c));
            }
            parts.push_back("--" + long_name);
            for (const auto &alias : long_aliases)
            {
                parts.push_back("--" + alias);
            }
            return JoinStrings(parts, ", ");
        }

    } // namespace

    bool ParseResult::Has(std::string_view key) const
    {
        return values.find(ToKey(key)) != values.end();
    }

    // ParseResult 的访问器：方便按类型取值并处理默认/解析失败情况。

    std::vector<std::string> ParseResult::GetAll(std::string_view key) const
    {
        const auto it = values.find(ToKey(key));
        if (it == values.end())
        {
            return {};
        }
        return it->second;
    }

    std::string ParseResult::GetString(std::string_view key, std::string_view fallback) const
    {
        const auto it = values.find(ToKey(key));
        if (it == values.end() || it->second.empty())
        {
            return std::string(fallback);
        }
        return it->second.back();
    }

    int ParseResult::GetInt(std::string_view key, int fallback) const
    {
        const std::string raw = GetString(key, "");
        if (raw.empty())
        {
            return fallback;
        }
        int parsed = 0;
        return TryParseInt(raw, &parsed) ? parsed : fallback;
    }

    double ParseResult::GetDouble(std::string_view key, double fallback) const
    {
        const std::string raw = GetString(key, "");
        if (raw.empty())
        {
            return fallback;
        }
        double parsed = 0.0;
        return TryParseDouble(raw, &parsed) ? parsed : fallback;
    }

    bool ParseResult::GetBool(std::string_view key, bool fallback) const
    {
        const std::string raw = GetString(key, "");
        if (raw.empty())
        {
            return fallback;
        }
        bool parsed = false;
        return TryParseBool(raw, &parsed) ? parsed : fallback;
    }

    int ParseResult::GetCount(std::string_view key) const
    {
        const auto it = values.find(ToKey(key));
        if (it == values.end())
        {
            return 0;
        }
        return static_cast<int>(it->second.size());
    }

    void SubcommandRouter::Register(std::string name, Handler handler, std::string description)
    {
        entries_.push_back({std::move(name), std::move(handler), std::move(description)});
    }

    // SubcommandRouter: 注册/分发子命令的简单实现，按注册顺序查找并执行对应 handler。

    int SubcommandRouter::Dispatch(const std::vector<std::string> &args, std::string *error) const
    {
        if (args.empty())
        {
            if (error != nullptr)
            {
                *error = "Missing subcommand.";
            }
            return kExitParseError;
        }

        const std::string &command_name = args.front();
        for (const auto &entry : entries_)
        {
            if (entry.name == command_name)
            {
                return entry.handler(args);
            }
        }

        if (error != nullptr)
        {
            *error = "Unknown subcommand: '" + command_name + "'.";
        }
        return kExitParseError;
    }

    std::vector<std::string> SubcommandRouter::Names() const
    {
        std::vector<std::string> names;
        names.reserve(entries_.size());
        for (const auto &entry : entries_)
        {
            names.push_back(entry.name);
        }
        return names;
    }

    std::string SubcommandRouter::DescriptionFor(std::string_view name) const
    {
        for (const auto &entry : entries_)
        {
            if (entry.name == name)
            {
                return entry.description;
            }
        }
        return "";
    }

    void SubcommandTree::RegisterRoot(std::string name, std::string description)
    {
        if (name.empty())
        {
            throw std::invalid_argument("Subcommand root name must not be empty.");
        }
        for (auto &root : roots_)
        {
            if (root.name == name)
            {
                if (!description.empty())
                {
                    root.description = std::move(description);
                }
                return;
            }
        }
        roots_.push_back({std::move(name), std::move(description), {}});
    }

    void SubcommandTree::RegisterLeaf(std::string root, std::string leaf, std::string description)
    {
        if (root.empty() || leaf.empty())
        {
            throw std::invalid_argument("Subcommand root/leaf name must not be empty.");
        }
        RegisterRoot(root, "");
        for (auto &entry : roots_)
        {
            if (entry.name != root)
            {
                continue;
            }
            for (auto &child : entry.leaves)
            {
                if (child.name == leaf)
                {
                    if (!description.empty())
                    {
                        child.description = std::move(description);
                    }
                    return;
                }
            }
            entry.leaves.push_back({std::move(leaf), std::move(description)});
            return;
        }
    }

    std::vector<std::string> SubcommandTree::Roots() const
    {
        std::vector<std::string> out;
        out.reserve(roots_.size());
        for (const auto &root : roots_)
        {
            out.push_back(root.name);
        }
        return out;
    }

    std::vector<std::string> SubcommandTree::Leaves(std::string_view root) const
    {
        for (const auto &entry : roots_)
        {
            if (entry.name != root)
            {
                continue;
            }
            std::vector<std::string> out;
            out.reserve(entry.leaves.size());
            for (const auto &leaf : entry.leaves)
            {
                out.push_back(leaf.name);
            }
            return out;
        }
        return {};
    }

    bool SubcommandTree::HasRoot(std::string_view root) const
    {
        for (const auto &entry : roots_)
        {
            if (entry.name == root)
            {
                return true;
            }
        }
        return false;
    }

    bool SubcommandTree::HasLeaf(std::string_view root, std::string_view leaf) const
    {
        for (const auto &entry : roots_)
        {
            if (entry.name != root)
            {
                continue;
            }
            for (const auto &child : entry.leaves)
            {
                if (child.name == leaf)
                {
                    return true;
                }
            }
        }
        return false;
    }

    std::string SubcommandTree::DescriptionForRoot(std::string_view root) const
    {
        for (const auto &entry : roots_)
        {
            if (entry.name == root)
            {
                return entry.description;
            }
        }
        return "";
    }

    std::string SubcommandTree::DescriptionForLeaf(std::string_view root, std::string_view leaf) const
    {
        for (const auto &entry : roots_)
        {
            if (entry.name != root)
            {
                continue;
            }
            for (const auto &child : entry.leaves)
            {
                if (child.name == leaf)
                {
                    return child.description;
                }
            }
        }
        return "";
    }

    Parser::OptionBuilder::OptionBuilder(Parser *parser, std::size_t index) : parser_(parser), index_(index) {}

    // OptionBuilder/PositionalBuilder: 流式构造器，便于链式 API 定义选项。

    Parser::OptionBuilder &Parser::OptionBuilder::String()
    {
        auto &spec = parser_->options_[index_];
        spec.value_type = ValueType::String;
        spec.takes_value = true;
        return *this;
    }

    Parser::OptionBuilder &Parser::OptionBuilder::Int()
    {
        auto &spec = parser_->options_[index_];
        spec.value_type = ValueType::Int;
        spec.takes_value = true;
        return *this;
    }

    Parser::OptionBuilder &Parser::OptionBuilder::Double()
    {
        auto &spec = parser_->options_[index_];
        spec.value_type = ValueType::Double;
        spec.takes_value = true;
        return *this;
    }

    Parser::OptionBuilder &Parser::OptionBuilder::BoolFlag()
    {
        auto &spec = parser_->options_[index_];
        spec.value_type = ValueType::Bool;
        spec.takes_value = false;
        spec.repeat_mode = RepeatMode::Override;
        spec.bool_mode = BoolFlagMode::Switch;
        spec.value_name.clear();
        return *this;
    }

    Parser::OptionBuilder &Parser::OptionBuilder::BoolMode(BoolFlagMode mode)
    {
        parser_->options_[index_].bool_mode = mode;
        return *this;
    }

    Parser::OptionBuilder &Parser::OptionBuilder::Alias(std::string long_alias)
    {
        parser_->options_[index_].long_aliases.push_back(std::move(long_alias));
        return *this;
    }

    Parser::OptionBuilder &Parser::OptionBuilder::Aliases(std::vector<std::string> long_aliases)
    {
        auto &slot = parser_->options_[index_].long_aliases;
        slot.insert(slot.end(), long_aliases.begin(), long_aliases.end());
        return *this;
    }

    Parser::OptionBuilder &Parser::OptionBuilder::ShortAlias(char short_alias)
    {
        parser_->options_[index_].short_aliases.push_back(short_alias);
        return *this;
    }

    Parser::OptionBuilder &Parser::OptionBuilder::ShortAliases(std::vector<char> short_aliases)
    {
        auto &slot = parser_->options_[index_].short_aliases;
        slot.insert(slot.end(), short_aliases.begin(), short_aliases.end());
        return *this;
    }

    Parser::OptionBuilder &Parser::OptionBuilder::ConvertWith(ValueConverter converter)
    {
        parser_->options_[index_].converter = std::move(converter);
        return *this;
    }

    Parser::OptionBuilder &Parser::OptionBuilder::OptionalValue(bool value)
    {
        auto &spec = parser_->options_[index_];
        spec.cardinality = value ? ValueCardinality::Optional : ValueCardinality::Single;
        if (value)
        {
            spec.required = false;
        }
        return *this;
    }

    Parser::OptionBuilder &Parser::OptionBuilder::ListValue(bool value)
    {
        auto &spec = parser_->options_[index_];
        if (value)
        {
            spec.cardinality = ValueCardinality::List;
            spec.repeat_mode = RepeatMode::Append;
            spec.required = false;
        }
        else
        {
            spec.cardinality = ValueCardinality::Single;
        }
        return *this;
    }

    Parser::OptionBuilder &Parser::OptionBuilder::Required(bool value)
    {
        parser_->options_[index_].required = value;
        return *this;
    }

    Parser::OptionBuilder &Parser::OptionBuilder::Repeat(RepeatMode mode)
    {
        parser_->options_[index_].repeat_mode = mode;
        return *this;
    }

    Parser::OptionBuilder &Parser::OptionBuilder::Default(std::string value)
    {
        parser_->options_[index_].default_value = std::move(value);
        return *this;
    }

    Parser::OptionBuilder &Parser::OptionBuilder::ValueName(std::string value_name)
    {
        parser_->options_[index_].value_name = std::move(value_name);
        return *this;
    }

    Parser::OptionBuilder &Parser::OptionBuilder::Description(std::string description)
    {
        parser_->options_[index_].description = std::move(description);
        return *this;
    }

    Parser::OptionBuilder &Parser::OptionBuilder::Range(double min_value, double max_value, RangePolicy policy)
    {
        auto &spec = parser_->options_[index_];
        spec.min_value = min_value;
        spec.max_value = max_value;
        spec.range_policy = policy;
        return *this;
    }

    Parser::OptionBuilder &Parser::OptionBuilder::Choices(std::vector<std::string> choices)
    {
        parser_->options_[index_].choices = std::move(choices);
        return *this;
    }

    Parser &Parser::OptionBuilder::Done()
    {
        auto &spec = parser_->options_[index_];
        if (spec.repeat_mode == RepeatMode::Append && spec.cardinality == ValueCardinality::Single)
        {
            spec.cardinality = ValueCardinality::List;
        }
        if (spec.value_type == ValueType::Bool && !spec.takes_value)
        {
            spec.repeat_mode = RepeatMode::Override;
            if (spec.default_value.has_value() && !spec.default_value->empty())
            {
                spec.default_value = ToLowerCopy(*spec.default_value);
            }
        }
        else
        {
            if (spec.value_name.empty())
            {
                spec.value_name = ToUpperIdentifier(spec.long_name);
            }
        }

        if (spec.description.empty())
        {
            spec.description = "Option '" + spec.long_name + "'.";
        }

        parser_->ValidateOptionSpecOrThrow(parser_->options_[index_], index_);
        return *parser_;
    }

    Parser::PositionalBuilder::PositionalBuilder(Parser *parser, std::size_t index) : parser_(parser), index_(index) {}

    Parser::PositionalBuilder &Parser::PositionalBuilder::String()
    {
        parser_->positionals_[index_].value_type = ValueType::String;
        return *this;
    }

    Parser::PositionalBuilder &Parser::PositionalBuilder::Int()
    {
        parser_->positionals_[index_].value_type = ValueType::Int;
        return *this;
    }

    Parser::PositionalBuilder &Parser::PositionalBuilder::Double()
    {
        parser_->positionals_[index_].value_type = ValueType::Double;
        return *this;
    }

    Parser::PositionalBuilder &Parser::PositionalBuilder::ConvertWith(ValueConverter converter)
    {
        parser_->positionals_[index_].converter = std::move(converter);
        return *this;
    }

    Parser::PositionalBuilder &Parser::PositionalBuilder::OptionalValue(bool value)
    {
        auto &spec = parser_->positionals_[index_];
        spec.cardinality = value ? ValueCardinality::Optional : ValueCardinality::Single;
        if (value)
        {
            spec.required = false;
        }
        return *this;
    }

    Parser::PositionalBuilder &Parser::PositionalBuilder::ListValue(bool value)
    {
        auto &spec = parser_->positionals_[index_];
        if (value)
        {
            spec.cardinality = ValueCardinality::List;
            spec.required = false;
            spec.variadic = true;
        }
        else
        {
            spec.cardinality = ValueCardinality::Single;
        }
        return *this;
    }

    Parser::PositionalBuilder &Parser::PositionalBuilder::Required(bool value)
    {
        parser_->positionals_[index_].required = value;
        return *this;
    }

    Parser::PositionalBuilder &Parser::PositionalBuilder::Variadic(bool value)
    {
        parser_->positionals_[index_].variadic = value;
        return *this;
    }

    Parser::PositionalBuilder &Parser::PositionalBuilder::Default(std::string value)
    {
        parser_->positionals_[index_].default_value = std::move(value);
        return *this;
    }

    Parser::PositionalBuilder &Parser::PositionalBuilder::Description(std::string description)
    {
        parser_->positionals_[index_].description = std::move(description);
        return *this;
    }

    Parser::PositionalBuilder &Parser::PositionalBuilder::Range(double min_value, double max_value, RangePolicy policy)
    {
        auto &spec = parser_->positionals_[index_];
        spec.min_value = min_value;
        spec.max_value = max_value;
        spec.range_policy = policy;
        return *this;
    }

    Parser::PositionalBuilder &Parser::PositionalBuilder::Choices(std::vector<std::string> choices)
    {
        parser_->positionals_[index_].choices = std::move(choices);
        return *this;
    }

    Parser &Parser::PositionalBuilder::Done()
    {
        auto &spec = parser_->positionals_[index_];
        if (spec.variadic && spec.cardinality == ValueCardinality::Single)
        {
            spec.cardinality = ValueCardinality::List;
        }
        if (spec.description.empty())
        {
            spec.description = "Positional argument '" + spec.name + "'.";
        }

        parser_->ValidatePositionalSpecOrThrow(parser_->positionals_[index_], index_);
        return *parser_;
    }

    Parser::Parser() = default;

    Parser &Parser::SetProgramName(std::string name)
    {
        program_name_ = std::move(name);
        return *this;
    }

    Parser &Parser::SetDescription(std::string description)
    {
        description_ = std::move(description);
        return *this;
    }

    Parser &Parser::SetUsageExample(std::string usage_example)
    {
        usage_example_ = std::move(usage_example);
        return *this;
    }

    Parser &Parser::SetLogger(IParseLogger *logger)
    {
        logger_ = logger;
        return *this;
    }

    Parser &Parser::EnableTrace(bool value)
    {
        trace_enabled_ = value;
        return *this;
    }

    Parser &Parser::SetHelpLayout(HelpLayout layout)
    {
        help_layout_ = layout;
        return *this;
    }

    Parser &Parser::EnableLegacyProfile(bool value)
    {
        legacy_profile_enabled_ = value;
        if (legacy_profile_enabled_)
        {
            help_layout_ = HelpLayout::Compact;
        }
        return *this;
    }

    Parser::OptionBuilder Parser::Option(std::string long_name, char short_name)
    {
        long_name = NormalizeLongName(long_name);
        if (long_name.empty())
        {
            throw std::invalid_argument("Option long_name must not be empty.");
        }

        for (const auto &ch : long_name)
        {
            const bool ok = std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '-' || ch == '_';
            if (!ok)
            {
                throw std::invalid_argument("Option long_name contains invalid characters: '" + long_name + "'.");
            }
        }

        for (const auto &existing : options_)
        {
            if (existing.long_name == long_name ||
                std::find(existing.long_aliases.begin(), existing.long_aliases.end(), long_name) != existing.long_aliases.end())
            {
                throw std::invalid_argument("Duplicate option long_name: '--" + long_name + "'.");
            }
            if (short_name != '\0' &&
                (existing.short_name == short_name ||
                 std::find(existing.short_aliases.begin(), existing.short_aliases.end(), short_name) != existing.short_aliases.end()))
            {
                throw std::invalid_argument("Duplicate option short_name: '-" + std::string(1, short_name) + "'.");
            }
        }

        for (const auto &positional : positionals_)
        {
            if (positional.name == long_name)
            {
                throw std::invalid_argument("Option long_name conflicts with positional name: '" + long_name + "'.");
            }
        }

        OptionSpec spec;
        spec.long_name = std::move(long_name);
        spec.short_name = short_name;
        options_.push_back(std::move(spec));
        return OptionBuilder(this, options_.size() - 1);
    }

    Parser::OptionBuilder Parser::Flag(std::string long_name, char short_name)
    {
        auto builder = Option(std::move(long_name), short_name);
        return builder.BoolFlag();
    }

    Parser::PositionalBuilder Parser::Positional(std::string name)
    {
        if (name.empty())
        {
            throw std::invalid_argument("Positional name must not be empty.");
        }

        for (const auto &existing : positionals_)
        {
            if (existing.name == name)
            {
                throw std::invalid_argument("Duplicate positional name: '" + name + "'.");
            }
        }

        for (const auto &existing : options_)
        {
            if (existing.long_name == name)
            {
                throw std::invalid_argument("Positional name conflicts with option long_name: '" + name + "'.");
            }
        }

        if (!positionals_.empty() && positionals_.back().variadic)
        {
            throw std::invalid_argument("Variadic positional must be declared last.");
        }

        PositionalSpec spec;
        spec.name = std::move(name);
        positionals_.push_back(std::move(spec));
        return PositionalBuilder(this, positionals_.size() - 1);
    }

    Parser &Parser::AddOptionTemplate(const OptionTemplate &tpl)
    {
        OptionBuilder builder = Option(tpl.long_name, tpl.short_name);

        switch (tpl.value_type)
        {
        case ValueType::String:
            builder.String();
            break;
        case ValueType::Int:
            builder.Int();
            break;
        case ValueType::Double:
            builder.Double();
            break;
        case ValueType::Bool:
            builder.BoolFlag().BoolMode(tpl.bool_mode);
            break;
        }

        builder.Required(tpl.required)
            .Repeat(tpl.repeat_mode)
            .Description(tpl.description)
            .Choices(tpl.choices);

        for (const auto &alias : tpl.long_aliases)
        {
            builder.Alias(alias);
        }
        for (char alias : tpl.short_aliases)
        {
            builder.ShortAlias(alias);
        }

        options_.back().min_value = tpl.min_value;
        options_.back().max_value = tpl.max_value;
        options_.back().range_policy = tpl.range_policy;

        if (tpl.value_name.empty())
        {
            if (tpl.value_type != ValueType::Bool)
            {
                builder.ValueName("VALUE");
            }
        }
        else
        {
            builder.ValueName(tpl.value_name);
        }

        if (tpl.default_value.has_value())
        {
            builder.Default(*tpl.default_value);
        }

        return builder.Done();
    }

    Parser &Parser::AddPositionalTemplate(const PositionalTemplate &tpl)
    {
        PositionalBuilder builder = Positional(tpl.name);
        switch (tpl.value_type)
        {
        case ValueType::String:
            builder.String();
            break;
        case ValueType::Int:
            builder.Int();
            break;
        case ValueType::Double:
            builder.Double();
            break;
        case ValueType::Bool:
            throw std::invalid_argument("Positional bool is not supported in template API.");
        }

        builder.Required(tpl.required)
            .Variadic(tpl.variadic)
            .Description(tpl.description)
            .Choices(tpl.choices);

        positionals_.back().min_value = tpl.min_value;
        positionals_.back().max_value = tpl.max_value;
        positionals_.back().range_policy = tpl.range_policy;

        if (tpl.default_value.has_value())
        {
            builder.Default(*tpl.default_value);
        }

        return builder.Done();
    }

    Parser &Parser::AddMutexGroup(MutexGroup group)
    {
        if (group.option_names.size() < 2)
        {
            throw std::invalid_argument("MutexGroup must contain at least two options.");
        }

        for (const auto &option_name : group.option_names)
        {
            bool found = false;
            for (const auto &option : options_)
            {
                if (option.long_name == option_name)
                {
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                throw std::invalid_argument("MutexGroup references unknown option: '--" + option_name + "'.");
            }
        }

        mutex_groups_.push_back(std::move(group));
        return *this;
    }

    Parser &Parser::AddDependency(DependencyRule rule)
    {
        if (rule.option_name.empty() || rule.required_option_name.empty())
        {
            throw std::invalid_argument("DependencyRule fields must not be empty.");
        }

        bool left_found = false;
        bool right_found = false;
        for (const auto &option : options_)
        {
            if (option.long_name == rule.option_name)
            {
                left_found = true;
            }
            if (option.long_name == rule.required_option_name)
            {
                right_found = true;
            }
        }
        if (!left_found)
        {
            throw std::invalid_argument("DependencyRule references unknown option: '--" + rule.option_name + "'.");
        }
        if (!right_found)
        {
            throw std::invalid_argument("DependencyRule references unknown required option: '--" + rule.required_option_name + "'.");
        }

        dependency_rules_.push_back(std::move(rule));
        return *this;
    }

    Parser &Parser::AddConstraintRule(ConstraintRule rule)
    {
        if (rule.name.empty())
        {
            throw std::invalid_argument("ConstraintRule name must not be empty.");
        }
        if (!rule.evaluator)
        {
            throw std::invalid_argument("ConstraintRule evaluator must be provided.");
        }

        constraint_rules_.push_back(std::move(rule));
        return *this;
    }

    Parser &Parser::SetGlobalConverter(ValueType type, ValueConverter converter)
    {
        if (!converter)
        {
            throw std::invalid_argument("Global converter must be valid.");
        }

        switch (type)
        {
        case ValueType::String:
            global_string_converter_ = std::move(converter);
            break;
        case ValueType::Int:
            global_int_converter_ = std::move(converter);
            break;
        case ValueType::Double:
            global_double_converter_ = std::move(converter);
            break;
        case ValueType::Bool:
            global_bool_converter_ = std::move(converter);
            break;
        }
        return *this;
    }

    Parser &Parser::SetUnknownOptionHandler(UnknownOptionHandler handler)
    {
        unknown_option_handler_ = std::move(handler);
        return *this;
    }

    Parser &Parser::AddSubcommandRoot(std::string name, std::string description)
    {
        subcommand_tree_.RegisterRoot(std::move(name), std::move(description));
        return *this;
    }

    Parser &Parser::AddSubcommandLeaf(std::string root, std::string leaf, std::string description)
    {
        subcommand_tree_.RegisterLeaf(std::move(root), std::move(leaf), std::move(description));
        return *this;
    }

    void Parser::ValidateOptionSpecOrThrow(const OptionSpec &spec, std::size_t index) const
    {
        (void)index;
        if (spec.long_name.empty())
        {
            throw std::invalid_argument("Option long_name must not be empty.");
        }

        if (spec.short_name != '\0' && !std::isalnum(static_cast<unsigned char>(spec.short_name)))
        {
            throw std::invalid_argument("Option short_name must be alphanumeric when provided.");
        }

        std::vector<std::string> normalized_long_aliases;
        normalized_long_aliases.reserve(spec.long_aliases.size());
        for (const auto &raw_alias : spec.long_aliases)
        {
            const std::string alias = NormalizeLongName(raw_alias);
            if (alias.empty())
            {
                throw std::invalid_argument("Option long alias must not be empty for '--" + spec.long_name + "'.");
            }
            if (alias == spec.long_name)
            {
                throw std::invalid_argument("Option long alias duplicates long_name for '--" + spec.long_name + "'.");
            }
            if (std::find(normalized_long_aliases.begin(), normalized_long_aliases.end(), alias) != normalized_long_aliases.end())
            {
                throw std::invalid_argument("Duplicate long alias for '--" + spec.long_name + "'.");
            }
            normalized_long_aliases.push_back(alias);
        }

        std::vector<char> normalized_short_aliases;
        normalized_short_aliases.reserve(spec.short_aliases.size());
        for (char alias : spec.short_aliases)
        {
            if (!std::isalnum(static_cast<unsigned char>(alias)))
            {
                throw std::invalid_argument("Option short alias must be alphanumeric for '--" + spec.long_name + "'.");
            }
            if (alias == spec.short_name)
            {
                throw std::invalid_argument("Option short alias duplicates short_name for '--" + spec.long_name + "'.");
            }
            if (std::find(normalized_short_aliases.begin(), normalized_short_aliases.end(), alias) != normalized_short_aliases.end())
            {
                throw std::invalid_argument("Duplicate short alias for '--" + spec.long_name + "'.");
            }
            normalized_short_aliases.push_back(alias);
        }

        if (spec.value_type == ValueType::Bool && spec.takes_value)
        {
            throw std::invalid_argument("Bool option must be declared as flag without explicit value.");
        }

        if (spec.value_type != ValueType::Bool && !spec.takes_value)
        {
            throw std::invalid_argument("Only bool option can be value-less flag.");
        }

        if (spec.value_type == ValueType::Bool && spec.repeat_mode == RepeatMode::Append)
        {
            throw std::invalid_argument("Bool flag does not support RepeatMode::Append.");
        }

        if (spec.cardinality == ValueCardinality::List && spec.repeat_mode != RepeatMode::Append)
        {
            throw std::invalid_argument("List option must use RepeatMode::Append.");
        }

        if (spec.cardinality == ValueCardinality::Optional && spec.required)
        {
            throw std::invalid_argument("Optional option cannot be required.");
        }

        if (spec.min_value.has_value() && spec.max_value.has_value() && *spec.min_value > *spec.max_value)
        {
            throw std::invalid_argument("Option range min must be <= max for '--" + spec.long_name + "'.");
        }

        if (spec.range_policy == RangePolicy::UseDefaultAndWarn &&
            (spec.min_value.has_value() || spec.max_value.has_value()) &&
            !spec.default_value.has_value())
        {
            throw std::invalid_argument("RangePolicy::UseDefaultAndWarn requires default value for '--" + spec.long_name + "'.");
        }

        if (spec.value_type == ValueType::Int)
        {
            const double int_min = static_cast<double>(std::numeric_limits<int>::min());
            const double int_max = static_cast<double>(std::numeric_limits<int>::max());
            if ((spec.min_value.has_value() && *spec.min_value < int_min) ||
                (spec.max_value.has_value() && *spec.max_value > int_max))
            {
                throw std::invalid_argument("Int option range exceeds int32 bounds for '--" + spec.long_name + "'.");
            }
        }

        auto validate_scalar = [&](const std::string &text, std::string_view source)
        {
            if (spec.value_type == ValueType::String)
            {
                return;
            }
            if (spec.value_type == ValueType::Int)
            {
                int parsed = 0;
                if (!TryParseInt(text, &parsed))
                {
                    throw std::invalid_argument("Invalid int " + std::string(source) + " for '--" + spec.long_name + "'.");
                }
                if ((spec.min_value.has_value() && static_cast<double>(parsed) < *spec.min_value) ||
                    (spec.max_value.has_value() && static_cast<double>(parsed) > *spec.max_value))
                {
                    throw std::invalid_argument("Int " + std::string(source) + " out of range for '--" + spec.long_name + "'.");
                }
                return;
            }
            if (spec.value_type == ValueType::Double)
            {
                double parsed = 0.0;
                if (!TryParseDouble(text, &parsed))
                {
                    throw std::invalid_argument("Invalid double " + std::string(source) + " for '--" + spec.long_name + "'.");
                }
                if ((spec.min_value.has_value() && parsed < *spec.min_value) ||
                    (spec.max_value.has_value() && parsed > *spec.max_value))
                {
                    throw std::invalid_argument("Double " + std::string(source) + " out of range for '--" + spec.long_name + "'.");
                }
                return;
            }
            if (spec.value_type == ValueType::Bool)
            {
                bool parsed = false;
                if (!TryParseBool(text, &parsed))
                {
                    throw std::invalid_argument("Invalid bool " + std::string(source) + " for '--" + spec.long_name + "'.");
                }
            }
        };

        if (spec.default_value.has_value())
        {
            validate_scalar(*spec.default_value, "default");
        }

        if (!spec.choices.empty())
        {
            std::vector<std::string> normalized_choices;
            normalized_choices.reserve(spec.choices.size());
            for (const auto &choice : spec.choices)
            {
                validate_scalar(choice, "choice");
                const std::string normalized = ToLowerCopy(choice);
                if (std::find(normalized_choices.begin(), normalized_choices.end(), normalized) != normalized_choices.end())
                {
                    throw std::invalid_argument("Duplicate choice (case-insensitive) for '--" + spec.long_name + "'.");
                }
                normalized_choices.push_back(normalized);
            }

            if (spec.default_value.has_value())
            {
                bool ok = false;
                for (const auto &choice : spec.choices)
                {
                    if (EqualsIgnoreCase(*spec.default_value, choice))
                    {
                        ok = true;
                        break;
                    }
                }
                if (!ok)
                {
                    throw std::invalid_argument("Default value is not in choices for '--" + spec.long_name + "'.");
                }
            }
        }
    }

    void Parser::ValidatePositionalSpecOrThrow(const PositionalSpec &spec, std::size_t index) const
    {
        if (spec.name.empty())
        {
            throw std::invalid_argument("Positional name must not be empty.");
        }

        if (spec.variadic && index + 1 != positionals_.size())
        {
            throw std::invalid_argument("Variadic positional must be the last declared positional.");
        }

        if (spec.min_value.has_value() && spec.max_value.has_value() && *spec.min_value > *spec.max_value)
        {
            throw std::invalid_argument("Positional range min must be <= max for '" + spec.name + "'.");
        }

        if (spec.range_policy == RangePolicy::UseDefaultAndWarn &&
            (spec.min_value.has_value() || spec.max_value.has_value()) &&
            !spec.default_value.has_value())
        {
            throw std::invalid_argument("RangePolicy::UseDefaultAndWarn requires default value for positional '" + spec.name + "'.");
        }

        if (spec.value_type == ValueType::Bool)
        {
            throw std::invalid_argument("Positional bool type is not supported.");
        }

        if (spec.cardinality == ValueCardinality::List && !spec.variadic)
        {
            throw std::invalid_argument("List positional must be variadic.");
        }

        if (spec.cardinality == ValueCardinality::Optional && spec.required)
        {
            throw std::invalid_argument("Optional positional cannot be required.");
        }

        auto validate_scalar = [&](const std::string &text, std::string_view source)
        {
            if (spec.value_type == ValueType::String)
            {
                return;
            }
            if (spec.value_type == ValueType::Int)
            {
                int parsed = 0;
                if (!TryParseInt(text, &parsed))
                {
                    throw std::invalid_argument("Invalid int " + std::string(source) + " for positional '" + spec.name + "'.");
                }
                return;
            }
            if (spec.value_type == ValueType::Double)
            {
                double parsed = 0.0;
                if (!TryParseDouble(text, &parsed))
                {
                    throw std::invalid_argument("Invalid double " + std::string(source) + " for positional '" + spec.name + "'.");
                }
            }
        };

        if (spec.default_value.has_value())
        {
            validate_scalar(*spec.default_value, "default");
        }
        std::vector<std::string> normalized_choices;
        normalized_choices.reserve(spec.choices.size());
        for (const auto &choice : spec.choices)
        {
            validate_scalar(choice, "choice");
            const std::string normalized = ToLowerCopy(choice);
            if (std::find(normalized_choices.begin(), normalized_choices.end(), normalized) != normalized_choices.end())
            {
                throw std::invalid_argument("Duplicate choice (case-insensitive) for positional '" + spec.name + "'.");
            }
            normalized_choices.push_back(normalized);
        }
    }

    void Parser::ValidateConfigurationOrThrow() const
    {
        std::vector<std::string> seen_long;
        std::vector<char> seen_short;

        for (std::size_t i = 0; i < options_.size(); ++i)
        {
            ValidateOptionSpecOrThrow(options_[i], i);

            auto try_add_long = [&](const std::string &name)
            {
                if (std::find(seen_long.begin(), seen_long.end(), name) != seen_long.end())
                {
                    throw std::invalid_argument("Duplicate option name/alias detected: '--" + name + "'.");
                }
                seen_long.push_back(name);
            };
            auto try_add_short = [&](char name)
            {
                if (name == '\0')
                {
                    return;
                }
                if (std::find(seen_short.begin(), seen_short.end(), name) != seen_short.end())
                {
                    throw std::invalid_argument("Duplicate option short name/alias detected: '-" + std::string(1, name) + "'.");
                }
                seen_short.push_back(name);
            };

            try_add_long(options_[i].long_name);
            for (const auto &alias : options_[i].long_aliases)
            {
                try_add_long(NormalizeLongName(alias));
            }
            try_add_short(options_[i].short_name);
            for (char alias : options_[i].short_aliases)
            {
                try_add_short(alias);
            }
        }
        for (std::size_t i = 0; i < positionals_.size(); ++i)
        {
            ValidatePositionalSpecOrThrow(positionals_[i], i);

            if (std::find(seen_long.begin(), seen_long.end(), positionals_[i].name) != seen_long.end())
            {
                throw std::invalid_argument("Positional name conflicts with option alias: '" + positionals_[i].name + "'.");
            }
        }
    }

    ParseResult Parser::Fail(ParseResult result,
                             ParseErrorKind kind,
                             std::string field,
                             std::string token,
                             std::string message) const
    {
        ParseError error;
        error.kind = kind;
        error.field = std::move(field);
        error.token = std::move(token);
        error.message = std::move(message);

        result.ok = false;
        result.help_requested = false;
        result.exit_code = kExitParseError;
        result.error = error;

        if (logger_ != nullptr)
        {
            logger_->OnError(error);
        }

        Trace(result, "error", error.token, error.message);

        return result;
    }

    void Parser::Trace(ParseResult &result, std::string stage, std::string token, std::string detail) const
    {
        if (!trace_enabled_)
        {
            return;
        }
        result.trace.push_back({std::move(stage), std::move(token), std::move(detail)});
    }

    ParseResult Parser::ApplyConstraintPipeline(ParseResult result) const
    {
        std::vector<ConstraintRule> runtime_rules;
        runtime_rules.reserve(mutex_groups_.size() + dependency_rules_.size() + constraint_rules_.size());

        for (std::size_t i = 0; i < mutex_groups_.size(); ++i)
        {
            const MutexGroup group = mutex_groups_[i];
            ConstraintRule rule;
            rule.name = "builtin.mutex." + std::to_string(i);
            rule.group = RuleGroup::Relation;
            rule.priority = RulePriority::High;
            rule.fail_fast = true;
            rule.evaluator = [group](const ConstraintContext &context)
            {
                auto has_value = [&](std::string_view name)
                {
                    const auto it = context.result.values.find(ToKey(name));
                    return it != context.result.values.end() && !it->second.empty();
                };

                std::vector<std::string> active;
                for (const auto &option_name : group.option_names)
                {
                    if (has_value(option_name))
                    {
                        active.push_back(option_name);
                    }
                }

                if (active.size() <= 1)
                {
                    return ConstraintResult{};
                }

                ConstraintResult out;
                out.ok = false;
                out.error.kind = ParseErrorKind::MutexConflict;
                out.error.field = "mutex";
                out.error.token = JoinStrings(active, ",");
                out.error.message = !group.message.empty()
                                        ? group.message
                                        : "Mutually exclusive options conflict: --" + JoinStrings(active, ", --") + ".";
                return out;
            };
            runtime_rules.push_back(std::move(rule));
        }

        for (std::size_t i = 0; i < dependency_rules_.size(); ++i)
        {
            const DependencyRule dep = dependency_rules_[i];
            ConstraintRule rule;
            rule.name = "builtin.dependency." + std::to_string(i);
            rule.group = RuleGroup::Relation;
            rule.priority = RulePriority::Normal;
            rule.fail_fast = true;
            rule.evaluator = [dep](const ConstraintContext &context)
            {
                auto has_value = [&](std::string_view name)
                {
                    const auto it = context.result.values.find(ToKey(name));
                    return it != context.result.values.end() && !it->second.empty();
                };

                if (!has_value(dep.option_name) || has_value(dep.required_option_name))
                {
                    return ConstraintResult{};
                }

                ConstraintResult out;
                out.ok = false;
                out.error.kind = ParseErrorKind::DependencyError;
                out.error.field = dep.option_name;
                out.error.token = dep.required_option_name;
                out.error.message = !dep.message.empty()
                                        ? dep.message
                                        : "Option '--" + dep.option_name + "' requires '--" + dep.required_option_name + "'.";
                return out;
            };
            runtime_rules.push_back(std::move(rule));
        }

        for (const auto &custom_rule : constraint_rules_)
        {
            runtime_rules.push_back(custom_rule);
        }

        std::stable_sort(runtime_rules.begin(), runtime_rules.end(), [](const ConstraintRule &lhs, const ConstraintRule &rhs)
                         {
            const int lhs_group = static_cast<int>(lhs.group);
            const int rhs_group = static_cast<int>(rhs.group);
            if (lhs_group != rhs_group)
            {
                return lhs_group < rhs_group;
            }

            const int lhs_priority = static_cast<int>(lhs.priority);
            const int rhs_priority = static_cast<int>(rhs.priority);
            if (lhs_priority != rhs_priority)
            {
                return lhs_priority < rhs_priority;
            }

            return lhs.name < rhs.name; });

        std::optional<ParseError> deferred_error;
        for (const auto &rule : runtime_rules)
        {
            const ConstraintResult evaluated = rule.evaluator(ConstraintContext{result});
            if (evaluated.ok)
            {
                continue;
            }

            if (rule.fail_fast)
            {
                return Fail(std::move(result),
                            evaluated.error.kind,
                            evaluated.error.field,
                            evaluated.error.token,
                            evaluated.error.message);
            }

            if (!deferred_error.has_value())
            {
                deferred_error = evaluated.error;
            }
            if (logger_ != nullptr)
            {
                logger_->OnWarning("Constraint rule '" + rule.name + "' failed and was deferred.");
            }
        }

        if (deferred_error.has_value())
        {
            return Fail(std::move(result),
                        deferred_error->kind,
                        deferred_error->field,
                        deferred_error->token,
                        deferred_error->message);
        }

        return result;
    }

    ParseResult Parser::Parse(int argc, const char *const argv[]) const
    {
        ParseResult result;
        result.ok = false;
        result.help_requested = false;
        result.exit_code = kExitParseError;

        const std::string effective_program_name =
            !program_name_.empty() ? program_name_ : (argc > 0 && argv[0] != nullptr ? std::string(argv[0]) : std::string("program"));
        runtime_program_name_ = effective_program_name;

        try
        {
            ValidateConfigurationOrThrow();
        }
        catch (const std::exception &ex)
        {
            return Fail(result, ParseErrorKind::InternalError, "config", "config", ex.what());
        }

        auto find_long_option = [&](const std::string &name) -> const OptionSpec *
        {
            const std::string normalized = NormalizeLongName(name);
            for (const auto &option : options_)
            {
                if (option.long_name == normalized)
                {
                    return &option;
                }
                for (const auto &alias : option.long_aliases)
                {
                    if (NormalizeLongName(alias) == normalized)
                    {
                        return &option;
                    }
                }
            }
            return nullptr;
        };

        auto find_short_option = [&](char name) -> const OptionSpec *
        {
            for (const auto &option : options_)
            {
                if (option.short_name == name)
                {
                    return &option;
                }
                if (std::find(option.short_aliases.begin(), option.short_aliases.end(), name) != option.short_aliases.end())
                {
                    return &option;
                }
            }
            return nullptr;
        };

        auto validate_typed_value = [&](std::string value,
                                        ValueType type,
                                        std::string_view field,
                                        std::string_view token,
                                        std::optional<double> min_value,
                                        std::optional<double> max_value,
                                        const std::vector<std::string> &choices,
                                        RangePolicy range_policy,
                                        const std::optional<std::string> &default_value,
                                        const std::optional<ValueConverter> &local_converter,
                                        std::string *normalized,
                                        ParseResult current_result) -> std::optional<ParseResult>
        {
            ValueConverter converter;
            if (local_converter.has_value())
            {
                converter = *local_converter;
            }
            else
            {
                switch (type)
                {
                case ValueType::String:
                    converter = global_string_converter_.has_value() ? *global_string_converter_ : ValueConverter(BuiltinStringConverter);
                    break;
                case ValueType::Int:
                    converter = global_int_converter_.has_value() ? *global_int_converter_ : ValueConverter(BuiltinIntConverter);
                    break;
                case ValueType::Double:
                    converter = global_double_converter_.has_value() ? *global_double_converter_ : ValueConverter(BuiltinDoubleConverter);
                    break;
                case ValueType::Bool:
                    converter = global_bool_converter_.has_value() ? *global_bool_converter_ : ValueConverter(BuiltinBoolConverter);
                    break;
                }
            }

            const ConvertResult converted = converter(value);
            if (!converted.ok)
            {
                return Fail(std::move(current_result),
                            ParseErrorKind::TypeMismatch,
                            std::string(field),
                            std::string(token),
                            converted.error.empty() ? "Value conversion failed." : converted.error);
            }
            value = converted.value;

            if (!choices.empty())
            {
                if (!ValidateChoiceIgnoreCase(value, choices))
                {
                    return Fail(std::move(current_result),
                                ParseErrorKind::ChoiceError,
                                std::string(field),
                                std::string(token),
                                "Value '" + value + "' is not in allowed choices: [" + JoinStrings(choices, ", ") + "].");
                }
                value = NormalizeChoice(value, choices);
            }

            auto fail_range = [&](double numeric_value) -> std::optional<ParseResult>
            {
                const bool low = min_value.has_value() && numeric_value < *min_value;
                const bool high = max_value.has_value() && numeric_value > *max_value;
                if (!low && !high)
                {
                    return std::nullopt;
                }

                if (range_policy == RangePolicy::UseDefaultAndWarn && default_value.has_value())
                {
                    *normalized = *default_value;
                    if (logger_ != nullptr)
                    {
                        logger_->OnWarning("Range violation on '" + std::string(field) + "', value '" + value + "' fallback to default '" + *default_value + "'.");
                    }
                    return std::nullopt;
                }

                return Fail(std::move(current_result),
                            ParseErrorKind::RangeError,
                            std::string(field),
                            std::string(token),
                            "Value '" + value + "' is outside allowed range (" + FormatRange(min_value, max_value) + ").");
            };

            if (type == ValueType::String)
            {
                *normalized = value;
                return std::nullopt;
            }

            if (type == ValueType::Int)
            {
                int parsed = 0;
                if (!TryParseInt(value, &parsed))
                {
                    return Fail(std::move(current_result),
                                ParseErrorKind::TypeMismatch,
                                std::string(field),
                                std::string(token),
                                "Invalid int value: '" + value + "'.");
                }
                if (auto range_err = fail_range(static_cast<double>(parsed)); range_err.has_value())
                {
                    return range_err;
                }
                if (normalized->empty())
                {
                    *normalized = value;
                }
                return std::nullopt;
            }

            if (type == ValueType::Double)
            {
                double parsed = 0.0;
                if (!TryParseDouble(value, &parsed))
                {
                    return Fail(std::move(current_result),
                                ParseErrorKind::TypeMismatch,
                                std::string(field),
                                std::string(token),
                                "Invalid double value: '" + value + "'.");
                }
                if (auto range_err = fail_range(parsed); range_err.has_value())
                {
                    return range_err;
                }
                if (normalized->empty())
                {
                    *normalized = value;
                }
                return std::nullopt;
            }

            if (type == ValueType::Bool)
            {
                *normalized = ToLowerCopy(value);
                return std::nullopt;
            }

            return Fail(std::move(current_result),
                        ParseErrorKind::InternalError,
                        std::string(field),
                        std::string(token),
                        "Unsupported value type.");
        };

        auto apply_value = [&](const OptionSpec &option, const std::string &value)
        {
            auto &slot = result.values[option.long_name];

            if (option.value_type == ValueType::Bool && !option.takes_value)
            {
                switch (option.bool_mode)
                {
                case BoolFlagMode::Switch:
                    slot.clear();
                    slot.push_back("true");
                    break;
                case BoolFlagMode::Count:
                    slot.push_back("1");
                    break;
                case BoolFlagMode::Toggle:
                {
                    bool current = false;
                    if (!slot.empty())
                    {
                        const std::string last = slot.back();
                        current = EqualsIgnoreCase(last, "true") || last == "1";
                    }
                    slot.clear();
                    slot.push_back(current ? "false" : "true");
                    break;
                }
                }
                return;
            }

            switch (option.repeat_mode)
            {
            case RepeatMode::Override:
                slot.clear();
                slot.push_back(value);
                break;
            case RepeatMode::Append:
                slot.push_back(value);
                break;
            case RepeatMode::Count:
                slot.push_back("1");
                break;
            }
        };

        auto apply_positional = [&](const PositionalSpec &positional, const std::string &value)
        {
            auto &slot = result.values[positional.name];
            if (positional.variadic)
            {
                slot.push_back(value);
            }
            else
            {
                slot.clear();
                slot.push_back(value);
            }
        };

        std::vector<std::string> positional_tokens;
        bool force_positionals = false;

        int start_index = 1;
        if (argc > 1 && argv[1] != nullptr)
        {
            const std::string first = argv[1];
            if (subcommand_tree_.HasRoot(first))
            {
                SubcommandPath path;
                path.root = first;
                Trace(result, "subcommand", first, "matched root command");
                start_index = 2;

                if (argc > 2 && argv[2] != nullptr)
                {
                    const std::string second = argv[2];
                    if (subcommand_tree_.HasLeaf(first, second))
                    {
                        path.leaf = second;
                        start_index = 3;
                        Trace(result, "subcommand", second, "matched leaf command");
                    }
                }

                result.subcommand_path = path;
                result.values["subcommand.root"] = {path.root};
                if (!path.leaf.empty())
                {
                    result.values["subcommand.leaf"] = {path.leaf};
                    result.values["subcommand.path"] = {path.root + "." + path.leaf};
                }
                else
                {
                    result.values["subcommand.path"] = {path.root};
                }
            }
        }

        for (int i = start_index; i < argc; ++i)
        {
            const std::string token = argv[i] == nullptr ? "" : std::string(argv[i]);
            Trace(result, "token", token, "scan");

            if (force_positionals)
            {
                positional_tokens.push_back(token);
                continue;
            }

            if (token == "--")
            {
                force_positionals = true;
                continue;
            }

            if (token == "-h" || token == "--help" || (legacy_profile_enabled_ && token == "-?"))
            {
                result.ok = true;
                result.help_requested = true;
                result.exit_code = kExitOk;
                Trace(result, "help", token, "help requested");
                return result;
            }

            if (token.size() > 2 && token.rfind("--", 0) == 0)
            {
                const std::size_t eq_pos = token.find('=');
                const std::string long_name = token.substr(2, eq_pos == std::string::npos ? std::string::npos : eq_pos - 2);
                const OptionSpec *option = find_long_option(long_name);
                if (option == nullptr)
                {
                    if (unknown_option_handler_)
                    {
                        std::string callback_error;
                        if (unknown_option_handler_(token, &callback_error))
                        {
                            continue;
                        }
                        if (!callback_error.empty())
                        {
                            return Fail(result, ParseErrorKind::UnknownOption, long_name, token, callback_error);
                        }
                    }
                    return Fail(result, ParseErrorKind::UnknownOption, long_name, token, "Unknown option: '--" + long_name + "'.");
                }

                if (!option->takes_value)
                {
                    if (eq_pos != std::string::npos)
                    {
                        return Fail(result,
                                    ParseErrorKind::TypeMismatch,
                                    option->long_name,
                                    token,
                                    "Flag '--" + option->long_name + "' does not accept a value.");
                    }
                    apply_value(*option, "true");
                    continue;
                }

                std::string raw_value;
                if (eq_pos != std::string::npos)
                {
                    raw_value = token.substr(eq_pos + 1);
                }
                else
                {
                    if (i + 1 >= argc)
                    {
                        return Fail(result,
                                    ParseErrorKind::MissingValue,
                                    option->long_name,
                                    token,
                                    "Missing value for option '--" + option->long_name + "'.");
                    }
                    ++i;
                    raw_value = argv[i] == nullptr ? "" : std::string(argv[i]);
                }

                std::string normalized;
                if (auto err = validate_typed_value(raw_value,
                                                    option->value_type,
                                                    option->long_name,
                                                    token,
                                                    option->min_value,
                                                    option->max_value,
                                                    option->choices,
                                                    option->range_policy,
                                                    option->default_value,
                                                    option->converter,
                                                    &normalized,
                                                    result);
                    err.has_value())
                {
                    return *err;
                }
                apply_value(*option, normalized);
                Trace(result, "option", option->long_name, "applied long option value");
                continue;
            }

            if (token.size() >= 2 && token[0] == '-' && token[1] != '-')
            {
                if (token.size() == 2)
                {
                    const char short_name = token[1];
                    const OptionSpec *option = find_short_option(short_name);
                    if (option == nullptr)
                    {
                        if (unknown_option_handler_)
                        {
                            std::string callback_error;
                            if (unknown_option_handler_(token, &callback_error))
                            {
                                continue;
                            }
                            if (!callback_error.empty())
                            {
                                return Fail(result, ParseErrorKind::UnknownOption, std::string(1, short_name), token, callback_error);
                            }
                        }
                        return Fail(result,
                                    ParseErrorKind::UnknownOption,
                                    std::string(1, short_name),
                                    token,
                                    "Unknown option: '-" + std::string(1, short_name) + "'.");
                    }

                    if (!option->takes_value)
                    {
                        apply_value(*option, "true");
                        continue;
                    }

                    if (i + 1 >= argc)
                    {
                        return Fail(result,
                                    ParseErrorKind::MissingValue,
                                    option->long_name,
                                    token,
                                    "Missing value for option '-" + std::string(1, short_name) + "'.");
                    }

                    ++i;
                    const std::string raw_value = argv[i] == nullptr ? "" : std::string(argv[i]);
                    std::string normalized;
                    if (auto err = validate_typed_value(raw_value,
                                                        option->value_type,
                                                        option->long_name,
                                                        token,
                                                        option->min_value,
                                                        option->max_value,
                                                        option->choices,
                                                        option->range_policy,
                                                        option->default_value,
                                                        option->converter,
                                                        &normalized,
                                                        result);
                        err.has_value())
                    {
                        return *err;
                    }
                    apply_value(*option, normalized);
                    Trace(result, "option", option->long_name, "applied short option value");
                    continue;
                }

                std::vector<const OptionSpec *> merged;
                merged.reserve(token.size() - 1);
                bool all_flags = true;
                for (std::size_t j = 1; j < token.size(); ++j)
                {
                    const char short_name = token[j];
                    const OptionSpec *option = find_short_option(short_name);
                    if (option == nullptr)
                    {
                        return Fail(result,
                                    ParseErrorKind::UnknownOption,
                                    std::string(1, short_name),
                                    token,
                                    "Unknown option in merged flags: '-" + std::string(1, short_name) + "'.");
                    }
                    if (option->takes_value)
                    {
                        all_flags = false;
                    }
                    merged.push_back(option);
                }

                if (!all_flags)
                {
                    return Fail(result,
                                ParseErrorKind::TypeMismatch,
                                token,
                                token,
                                "Merged short options only support boolean flags.");
                }

                for (const OptionSpec *option : merged)
                {
                    apply_value(*option, "true");
                    Trace(result, "flag", option->long_name, "applied merged flag");
                }
                continue;
            }

            positional_tokens.push_back(token);
        }

        for (const auto &option : options_)
        {
            if (!result.Has(option.long_name) && option.default_value.has_value())
            {
                auto &slot = result.values[option.long_name];
                slot.clear();
                slot.push_back(*option.default_value);
            }
        }

        std::size_t token_index = 0;
        for (const auto &positional : positionals_)
        {
            if (positional.variadic)
            {
                while (token_index < positional_tokens.size())
                {
                    std::string normalized;
                    const std::string raw = positional_tokens[token_index++];
                    if (auto err = validate_typed_value(raw,
                                                        positional.value_type,
                                                        positional.name,
                                                        raw,
                                                        positional.min_value,
                                                        positional.max_value,
                                                        positional.choices,
                                                        positional.range_policy,
                                                        positional.default_value,
                                                        positional.converter,
                                                        &normalized,
                                                        result);
                        err.has_value())
                    {
                        return *err;
                    }
                    apply_positional(positional, normalized);
                }
                if (!result.Has(positional.name) && positional.default_value.has_value())
                {
                    apply_positional(positional, *positional.default_value);
                }
                if (positional.required && !result.Has(positional.name))
                {
                    return Fail(result,
                                ParseErrorKind::MissingRequired,
                                positional.name,
                                positional.name,
                                "Missing required positional argument: '" + positional.name + "'.");
                }
                continue;
            }

            if (token_index < positional_tokens.size())
            {
                std::string normalized;
                const std::string raw = positional_tokens[token_index++];
                if (auto err = validate_typed_value(raw,
                                                    positional.value_type,
                                                    positional.name,
                                                    raw,
                                                    positional.min_value,
                                                    positional.max_value,
                                                    positional.choices,
                                                    positional.range_policy,
                                                    positional.default_value,
                                                    positional.converter,
                                                    &normalized,
                                                    result);
                    err.has_value())
                {
                    return *err;
                }
                apply_positional(positional, normalized);
                continue;
            }

            if (positional.default_value.has_value())
            {
                apply_positional(positional, *positional.default_value);
                continue;
            }

            if (positional.required)
            {
                return Fail(result,
                            ParseErrorKind::MissingRequired,
                            positional.name,
                            positional.name,
                            "Missing required positional argument: '" + positional.name + "'.");
            }
        }

        if (token_index < positional_tokens.size())
        {
            return Fail(result,
                        ParseErrorKind::UnexpectedPositional,
                        "positional",
                        positional_tokens[token_index],
                        "Unexpected positional argument: '" + positional_tokens[token_index] + "'.");
        }

        for (const auto &option : options_)
        {
            if (option.required && !result.Has(option.long_name))
            {
                return Fail(result,
                            ParseErrorKind::MissingRequired,
                            option.long_name,
                            "--" + option.long_name,
                            "Missing required option: '--" + option.long_name + "'.");
            }
        }

        result = ApplyConstraintPipeline(std::move(result));
        if (result.error.has_value())
        {
            return result;
        }

        result.ok = true;
        result.help_requested = false;
        result.exit_code = kExitOk;
        Trace(result, "result", "ok", "parse success");
        return result;
    }

    std::string Parser::HelpText() const
    {
        return HelpText(help_layout_);
    }

    std::string Parser::HelpText(HelpLayout layout) const
    {
        const std::string effective_program_name = !program_name_.empty()
                                                       ? program_name_
                                                       : (!runtime_program_name_.empty() ? runtime_program_name_ : "program");

        std::ostringstream oss;
        if (!description_.empty())
        {
            oss << description_ << "\n\n";
        }

        oss << "Usage:\n";
        oss << "  " << effective_program_name;
        if (!options_.empty())
        {
            oss << " [options]";
        }
        for (const auto &positional : positionals_)
        {
            if (positional.required)
            {
                oss << (positional.variadic ? " <" + positional.name + "...>" : " <" + positional.name + ">");
            }
            else
            {
                oss << (positional.variadic ? " [" + positional.name + "...]" : " [" + positional.name + "]");
            }
        }
        if (!positionals_.empty())
        {
            oss << " [-- <extras...>]";
        }
        oss << "\n";

        if (!usage_example_.empty())
        {
            oss << "Example:\n";
            oss << "  " << usage_example_ << "\n";
        }

        if (layout == HelpLayout::Compact)
        {
            oss << "\nOptions (compact):\n";
            for (const auto &option : options_)
            {
                const std::string names = FormatOptionNames(option.long_name, option.short_name, option.long_aliases, option.short_aliases);
                if (option.takes_value)
                {
                    const std::string value_name = option.value_name.empty() ? "VALUE" : option.value_name;
                    oss << "  " << names << " <" << value_name << ">: " << option.description << "\n";
                }
                else
                {
                    oss << "  " << names << ": " << option.description << "\n";
                }
            }

            if (!positionals_.empty())
            {
                oss << "\nPositionals (compact):\n";
                for (const auto &positional : positionals_)
                {
                    oss << "  " << positional.name << (positional.variadic ? "..." : "") << ": " << positional.description << "\n";
                }
            }

            if (legacy_profile_enabled_)
            {
                oss << "\nLegacy profile: enabled (compat bridge)\n";
            }

            const auto roots = subcommand_tree_.Roots();
            if (!roots.empty())
            {
                oss << "\nSubcommand Tree (compact):\n";
                for (const auto &root : roots)
                {
                    oss << "  " << root;
                    const std::string desc = subcommand_tree_.DescriptionForRoot(root);
                    if (!desc.empty())
                    {
                        oss << " - " << desc;
                    }
                    oss << "\n";
                    for (const auto &leaf : subcommand_tree_.Leaves(root))
                    {
                        oss << "    - " << leaf;
                        const std::string leaf_desc = subcommand_tree_.DescriptionForLeaf(root, leaf);
                        if (!leaf_desc.empty())
                        {
                            oss << " : " << leaf_desc;
                        }
                        oss << "\n";
                    }
                }
            }

            return oss.str();
        }

        auto dash = [](std::size_t n)
        { return std::string(n, '-'); };

        struct FlagRow
        {
            std::string names;
            std::string required;
            std::string def;
            std::string behavior;
            std::string constraints;
            std::string desc;
        };
        struct ValueRow
        {
            std::string names;
            std::string type;
            std::string required;
            std::string def;
            std::string repeat;
            std::string constraints;
            std::string desc;
        };
        struct PosRow
        {
            std::string name;
            std::string type;
            std::string required;
            std::string def;
            std::string arity;
            std::string constraints;
            std::string desc;
        };

        std::vector<FlagRow> flag_rows;
        std::vector<ValueRow> value_rows;
        std::vector<PosRow> positional_rows;

        flag_rows.push_back({"-h, --help", "no", "none", "switch", "none", "Show this help message and exit."});

        for (const auto &option : options_)
        {
            const std::string names = FormatOptionNames(option.long_name, option.short_name, option.long_aliases, option.short_aliases);
            std::vector<std::string> constraints;
            if (option.min_value.has_value() || option.max_value.has_value())
            {
                constraints.push_back("range(" + FormatRange(option.min_value, option.max_value) + ")");
            }
            if (!option.choices.empty())
            {
                constraints.push_back("choices(" + JoinStrings(option.choices, "|") + ")");
            }
            const std::string constraints_text = constraints.empty() ? "none" : JoinStrings(constraints, "; ");

            if (!option.takes_value)
            {
                flag_rows.push_back({names,
                                     option.required ? "yes" : "no",
                                     option.default_value.has_value() ? *option.default_value : "none",
                                     BoolFlagModeName(option.bool_mode),
                                     constraints_text,
                                     option.description.empty() ? "none" : option.description});
            }
            else
            {
                std::string names_with_value = names;
                const std::string value_name = option.value_name.empty() ? "VALUE" : option.value_name;
                names_with_value += " <" + value_name + ">";

                value_rows.push_back({names_with_value,
                                      ValueTypeName(option.value_type),
                                      option.required ? "yes" : "no",
                                      option.default_value.has_value() ? *option.default_value : "none",
                                      RepeatModeName(option.repeat_mode),
                                      constraints_text,
                                      option.description.empty() ? "none" : option.description});
            }
        }

        for (const auto &positional : positionals_)
        {
            std::vector<std::string> constraints;
            if (positional.min_value.has_value() || positional.max_value.has_value())
            {
                constraints.push_back("range(" + FormatRange(positional.min_value, positional.max_value) + ")");
            }
            if (!positional.choices.empty())
            {
                constraints.push_back("choices(" + JoinStrings(positional.choices, "|") + ")");
            }

            positional_rows.push_back({positional.variadic ? positional.name + "..." : positional.name,
                                       ValueTypeName(positional.value_type),
                                       positional.required ? "yes" : "no",
                                       positional.default_value.has_value() ? *positional.default_value : "none",
                                       positional.variadic ? "variadic" : "single",
                                       constraints.empty() ? "none" : JoinStrings(constraints, "; "),
                                       positional.description.empty() ? "none" : positional.description});
        }

        auto print_seven_col = [&](std::string_view title,
                                   std::string_view c1,
                                   std::string_view c2,
                                   std::string_view c3,
                                   std::string_view c4,
                                   std::string_view c5,
                                   std::string_view c6,
                                   const std::vector<std::array<std::string, 6>> &rows)
        {
            if (rows.empty())
            {
                return;
            }

            std::size_t w1 = c1.size();
            std::size_t w2 = c2.size();
            std::size_t w3 = c3.size();
            std::size_t w4 = c4.size();
            std::size_t w5 = c5.size();
            std::size_t w6 = c6.size();
            for (const auto &r : rows)
            {
                w1 = std::max(w1, r[0].size());
                w2 = std::max(w2, r[1].size());
                w3 = std::max(w3, r[2].size());
                w4 = std::max(w4, r[3].size());
                w5 = std::max(w5, r[4].size());
                w6 = std::max(w6, r[5].size());
            }

            oss << "\n"
                << title << ":\n";
            oss << "  " << std::left << std::setw(static_cast<int>(w1)) << c1
                << ' ' << std::setw(static_cast<int>(w2)) << c2
                << ' ' << std::setw(static_cast<int>(w3)) << c3
                << ' ' << std::setw(static_cast<int>(w4)) << c4
                << ' ' << std::setw(static_cast<int>(w5)) << c5
                << ' ' << c6 << "\n";
            oss << "  " << dash(w1) << ' ' << dash(w2) << ' ' << dash(w3) << ' ' << dash(w4) << ' ' << dash(w5) << ' ' << dash(std::max<std::size_t>(w6, 20)) << "\n";

            const std::size_t desc_width = 100;
            for (const auto &r : rows)
            {
                const auto wrapped = WrapText(r[5], desc_width);
                oss << "  " << std::left << std::setw(static_cast<int>(w1)) << r[0]
                    << ' ' << std::setw(static_cast<int>(w2)) << r[1]
                    << ' ' << std::setw(static_cast<int>(w3)) << r[2]
                    << ' ' << std::setw(static_cast<int>(w4)) << r[3]
                    << ' ' << std::setw(static_cast<int>(w5)) << r[4]
                    << ' ' << wrapped.front() << "\n";
                for (std::size_t i = 1; i < wrapped.size(); ++i)
                {
                    oss << "  " << std::setw(static_cast<int>(w1)) << ""
                        << ' ' << std::setw(static_cast<int>(w2)) << ""
                        << ' ' << std::setw(static_cast<int>(w3)) << ""
                        << ' ' << std::setw(static_cast<int>(w4)) << ""
                        << ' ' << std::setw(static_cast<int>(w5)) << ""
                        << ' ' << wrapped[i] << "\n";
                }
            }
        };

        {
            std::vector<std::array<std::string, 6>> rows;
            rows.reserve(flag_rows.size());
            for (const auto &r : flag_rows)
            {
                rows.push_back({r.names, "bool", r.required, r.def, r.behavior, r.desc + " [" + r.constraints + "]"});
            }
            print_seven_col("Flags", "Flags", "Type", "Required", "Default", "Behavior", "Description [Constraints]", rows);
        }

        {
            std::vector<std::array<std::string, 6>> rows;
            rows.reserve(value_rows.size());
            for (const auto &r : value_rows)
            {
                rows.push_back({r.names, r.type, r.required, r.def, r.repeat, r.desc + " [" + r.constraints + "]"});
            }
            print_seven_col("Value Options", "Options", "Type", "Required", "Default", "Repeat", "Description [Constraints]", rows);
        }

        {
            std::vector<std::array<std::string, 6>> rows;
            rows.reserve(positional_rows.size());
            for (const auto &r : positional_rows)
            {
                rows.push_back({r.name, r.type, r.required, r.def, r.arity, r.desc + " [" + r.constraints + "]"});
            }
            print_seven_col("Positional Arguments", "Name", "Type", "Required", "Default", "Arity", "Description [Constraints]", rows);
        }

        if (!mutex_groups_.empty() || !dependency_rules_.empty())
        {
            oss << "\nRelations:\n";

            if (!mutex_groups_.empty())
            {
                oss << "  Mutex Groups:\n";
                oss << "    " << std::left << std::setw(30) << "Members" << ' ' << "Message" << "\n";
                oss << "    " << dash(30) << ' ' << dash(40) << "\n";
                for (const auto &group : mutex_groups_)
                {
                    std::vector<std::string> names;
                    names.reserve(group.option_names.size());
                    for (const auto &name : group.option_names)
                    {
                        names.push_back("--" + name);
                    }
                    oss << "    " << std::left << std::setw(30) << JoinStrings(names, " | ")
                        << ' ' << (group.message.empty() ? "none" : group.message) << "\n";
                }
            }

            if (!dependency_rules_.empty())
            {
                oss << "  Dependencies:\n";
                oss << "    " << std::left << std::setw(20) << "If Present" << ' '
                    << std::setw(20) << "Requires" << ' ' << "Message" << "\n";
                oss << "    " << dash(20) << ' ' << dash(20) << ' ' << dash(40) << "\n";
                for (const auto &rule : dependency_rules_)
                {
                    oss << "    " << std::left << std::setw(20) << ("--" + rule.option_name)
                        << ' ' << std::setw(20) << ("--" + rule.required_option_name)
                        << ' ' << (rule.message.empty() ? "none" : rule.message) << "\n";
                }
            }
        }

        const auto sub_names = subcommands_.Names();
        if (!sub_names.empty())
        {
            std::size_t w_name = std::string("Name").size();
            std::size_t w_desc = std::string("Description").size();
            for (const auto &name : sub_names)
            {
                w_name = std::max(w_name, name.size());
                w_desc = std::max(w_desc, subcommands_.DescriptionFor(name).size());
            }

            oss << "\nSubcommands:\n";
            oss << "  " << std::left << std::setw(static_cast<int>(w_name)) << "Name"
                << ' ' << "Description" << "\n";
            oss << "  " << dash(w_name) << ' ' << dash(std::max<std::size_t>(w_desc, 40)) << "\n";
            for (const auto &name : sub_names)
            {
                oss << "  " << std::left << std::setw(static_cast<int>(w_name)) << name
                    << ' ' << subcommands_.DescriptionFor(name) << "\n";
            }
        }

        const auto roots = subcommand_tree_.Roots();
        if (!roots.empty())
        {
            oss << "\nSubcommand Tree:\n";
            for (const auto &root : roots)
            {
                oss << "  " << root;
                const std::string root_desc = subcommand_tree_.DescriptionForRoot(root);
                if (!root_desc.empty())
                {
                    oss << " - " << root_desc;
                }
                oss << "\n";
                for (const auto &leaf : subcommand_tree_.Leaves(root))
                {
                    oss << "    * " << leaf;
                    const std::string leaf_desc = subcommand_tree_.DescriptionForLeaf(root, leaf);
                    if (!leaf_desc.empty())
                    {
                        oss << " : " << leaf_desc;
                    }
                    oss << "\n";
                }
            }
        }

        if (legacy_profile_enabled_)
        {
            oss << "\nLegacy profile: enabled (compat bridge).\n";
        }

        return oss.str();
    }

    SubcommandRouter &Parser::MutableSubcommands()
    {
        return subcommands_;
    }

    const SubcommandRouter &Parser::Subcommands() const
    {
        return subcommands_;
    }

    SubcommandTree &Parser::MutableSubcommandTree()
    {
        return subcommand_tree_;
    }

    const SubcommandTree &Parser::GetSubcommandTree() const
    {
        return subcommand_tree_;
    }

    std::string Parser::ResultToJson(const ParseResult &result, bool include_trace) const
    {
        std::ostringstream oss;
        oss << "{";
        oss << "\"schema\":\"argtool.parse.result\",";
        oss << "\"schema_version\":1,";
        oss << "\"ok\":" << (result.ok ? "true" : "false") << ",";
        oss << "\"help_requested\":" << (result.help_requested ? "true" : "false") << ",";
        oss << "\"exit_code\":" << result.exit_code;

        if (result.error.has_value())
        {
            oss << ",\"error\":{";
            oss << "\"kind\":" << static_cast<int>(result.error->kind) << ",";
            oss << "\"field\":\"" << JsonEscape(result.error->field) << "\",";
            oss << "\"token\":\"" << JsonEscape(result.error->token) << "\",";
            oss << "\"message\":\"" << JsonEscape(result.error->message) << "\"";
            oss << "}";
        }

        if (result.subcommand_path.has_value())
        {
            oss << ",\"subcommand\":{";
            oss << "\"root\":\"" << JsonEscape(result.subcommand_path->root) << "\",";
            oss << "\"leaf\":\"" << JsonEscape(result.subcommand_path->leaf) << "\"";
            oss << "}";
        }

        oss << ",\"values\":{";
        std::vector<std::tuple<std::string, std::vector<std::string>>> ordered_values;
        ordered_values.reserve(result.values.size());
        for (const auto &kv : result.values)
        {
            ordered_values.push_back({kv.first, kv.second});
        }
        std::sort(ordered_values.begin(), ordered_values.end(), [](const auto &lhs, const auto &rhs)
                  { return std::get<0>(lhs) < std::get<0>(rhs); });

        bool first_key = true;
        for (const auto &kv : ordered_values)
        {
            if (!first_key)
            {
                oss << ",";
            }
            first_key = false;
            const auto &key = std::get<0>(kv);
            const auto &vals = std::get<1>(kv);
            oss << "\"" << JsonEscape(key) << "\":[";
            for (std::size_t i = 0; i < vals.size(); ++i)
            {
                if (i > 0)
                {
                    oss << ",";
                }
                oss << "\"" << JsonEscape(vals[i]) << "\"";
            }
            oss << "]";
        }
        oss << "}";

        if (include_trace)
        {
            oss << ",\"trace\":[";
            for (std::size_t i = 0; i < result.trace.size(); ++i)
            {
                if (i > 0)
                {
                    oss << ",";
                }
                const auto &ev = result.trace[i];
                oss << "{";
                oss << "\"stage\":\"" << JsonEscape(ev.stage) << "\",";
                oss << "\"token\":\"" << JsonEscape(ev.token) << "\",";
                oss << "\"detail\":\"" << JsonEscape(ev.detail) << "\"";
                oss << "}";
            }
            oss << "]";
        }

        oss << "}";
        return oss.str();
    }

} // namespace argtool
