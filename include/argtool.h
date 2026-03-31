#pragma once

// 本文件为命令行参数解析库的公共头文件（中文注释）。
// 注意：请使用 UTF-8 无 BOM 编码以避免跨平台（Windows/Unix）编码不一致问题。
// 我在仓库根目录添加了 `.editorconfig` 来强制 UTF-8。

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace argtool
{

    enum class ValueType
    {
        String,
        Int,
        Double,
        Bool
    };
    // ValueType: 表示选项/位置参数的值类型。

    enum class RepeatMode
    {
        Override, // 后出现的值覆盖前一个值（默认）。
        Append,   // 将多个值追加到列表中，最终结果为 vector<string>。
        Count     // 统计出现次数，结果为一个字符串表示的整数。
    };
    // RepeatMode: 当选项多次出现时的合并策略：覆盖、追加或计数。

    enum class BoolFlagMode
    {
        Switch, // 以开关方式处理布尔 flag，出现即为 true（默认）。
        Count,  // 统计布尔 flag 出现的次数，结果为一个字符串表示的整数。
        Toggle  // 每次出现切换状态，第一次出现为 true，再次出现变为 false，以此类推。
    };
    // BoolFlagMode: 布尔 flag 的语义：开关、计数或切换。

    enum class RangePolicy
    {
        Fail,             // 数值超出范围时失败。
        UseDefaultAndWarn // 数值超出范围时使用默认值并发出警告。
    };
    // RangePolicy: 数值超出范围时的处理策略。

    enum class ParseErrorKind
    {
        None,                 // 无错误。
        UnknownOption,        // 遇到未定义的选项。
        MissingValue,         // 选项需要值但未提供。
        InvalidValue,         // 选项值无法解析为指定类型。
        MissingRequired,      // 缺少必需的选项或位置参数。
        TypeMismatch,         // 选项值类型不匹配。
        RangeError,           // 数值选项超出定义的范围。
        ChoiceError,          // 选项值不在允许的选择范围内。
        MutexConflict,        // 互斥选项同时出现。
        DependencyError,      // 选项依赖的另一个选项未出现。
        UnexpectedPositional, // 出现了未定义的额外位置参数。
        InternalError         // 解析器内部错误（如配置错误），不应发生。
    };
    // ParseErrorKind: 解析错误的分类，用于上报给调用方或 logger。

    enum class RulePriority
    {
        High = 0,
        Normal = 100,
        Low = 200
    };
    // RulePriority: 规则执行优先级，值越小越先执行。

    enum class RuleGroup
    {
        Semantic = 0,
        Relation = 100,
        Custom = 200
    };
    // RuleGroup: 规则分组，用于在同一优先级下稳定排序。

    struct ParseError
    {
        ParseErrorKind kind{ParseErrorKind::None}; // 错误类型。
        std::string field;                         // 相关字段（如选项名称或位置参数名称）。
        std::string token;                         // 导致错误的原始输入文本。
        std::string message;                       // 详细错误信息，便于调试和用户提示。
    };
    // ParseError: 解析过程中发生错误时的详细信息结构体。

    struct ParseResult;

    struct ConstraintContext
    {
        const ParseResult &result;
    };
    // ConstraintContext: 规则执行时可见的上下文，目前包含已解析结果。

    struct ConstraintResult
    {
        bool ok{true};
        ParseError error{};
    };
    // ConstraintResult: 规则执行结果，ok=false 时携带结构化错误。

    struct ConstraintRule
    {
        using Evaluator = std::function<ConstraintResult(const ConstraintContext &)>;

        std::string name;
        RulePriority priority{RulePriority::Normal};
        RuleGroup group{RuleGroup::Custom};
        bool fail_fast{true};
        Evaluator evaluator;
    };
    // ConstraintRule: 可扩展约束规则定义，支持分组和优先级。

    struct ConvertResult
    {
        bool ok{true};
        std::string value;
        std::string error;
    };
    // ConvertResult: converter 结果，ok=false 时 error 描述失败原因。

    using ValueConverter = std::function<ConvertResult(std::string_view raw)>;
    // ValueConverter: 值转换函数，输入原始文本，输出规范化字符串或错误。

    enum class ValueCardinality
    {
        Single,
        Optional,
        List
    };
    // ValueCardinality: 参数值基数语义，分别表示单值/可选值/列表值。

    enum class HelpLayout
    {
        Fixed,
        Compact
    };
    // HelpLayout: help 输出布局，Fixed 为表格，Compact 为紧凑列表。

    struct TraceEvent
    {
        std::string stage;
        std::string token;
        std::string detail;
    };
    // TraceEvent: 轻量 trace 事件。

    struct SubcommandPath
    {
        std::string root;
        std::string leaf;
    };
    // SubcommandPath: 两层子命令路径，leaf 可为空。

    class IParseLogger
    {
    public:
        virtual ~IParseLogger() = default;
        virtual void OnError(const ParseError &error) = 0;
        virtual void OnWarning(std::string_view message) = 0;
    };
    // IParseLogger: 可选的回调接口，解析器在遇到错误/警告时调用。

    struct OptionTemplate
    {
        std::string long_name;                        // 选项的长名称（如 "verbose" 对应 --verbose）。
        char short_name{'\0'};                        // 选项的短名称（如 'v' 对应 -v），默认为 '\0' 表示无短名称。
        std::vector<std::string> long_aliases;        // 选项的长名称别名列表（如 "help" 对应 --help 和 "h"）。
        std::vector<char> short_aliases;              // 选项的短名称别名列表（如 'h' 对应 -h 和 --help）。
        ValueType value_type{ValueType::String};      // 选项值的类型，默认为字符串。
        bool required{false};                         // 选项是否为必需，默认为 false。
        RepeatMode repeat_mode{RepeatMode::Override}; // 选项重复时的处理方式，默认为覆盖。
        BoolFlagMode bool_mode{BoolFlagMode::Switch}; // 布尔 flag 的语义，默认为开关。
        std::optional<std::string> default_value;     // 选项的默认值（如果有），类型为字符串，解析时会根据 value_type 转换。
        std::string value_name;                       // 选项值的名称，用于帮助文本中占位显示，默认为 "VALUE"。
        std::string description;                      // 选项的描述信息，用于帮助文本中说明选项的作用。
        std::optional<double> min_value;              // 数值选项的最小值（如果适用）。
        std::optional<double> max_value;              // 数值选项的最大值（如果适用）。
        std::vector<std::string> choices;             // 选项值的允许选择列表，如果非空则值必须在此列表中。
        RangePolicy range_policy{RangePolicy::Fail};  // 数值选项超出范围时的处理策略，默认为失败。
    };
    // OptionTemplate: 用于从配置或元数据快速创建 option 的模板结构。

    struct PositionalTemplate
    {
        std::string name;                            // 位置参数的名称（如 "input_file"），用于帮助文本和错误信息中。
        ValueType value_type{ValueType::String};     // 位置参数的值类型，默认为字符串。
        bool required{true};                         // 位置参数是否为必需，默认为 true。
        bool variadic{false};                        // 是否为可变参数，true 表示可以接受多个值并将它们作为列表提供给调用方。
        std::optional<std::string> default_value;    // 位置参数的默认值（如果有），仅在 variadic 为 false 时适用。
        std::string description;                     // 位置参数的描述信息，用于帮助文本中说明位置参数的作用。
        std::optional<double> min_value;             // 数值位置参数的最小值（如果适用）。
        std::optional<double> max_value;             // 数值位置参数的最大值（如果适用）。
        std::vector<std::string> choices;            // 位置参数的允许选择列表，如果非空则值必须在此列表中。
        RangePolicy range_policy{RangePolicy::Fail}; // 数值位置参数超出范围时的处理策略，默认为失败。
    };
    // PositionalTemplate: 用于描述位置参数（命令行中不以 -/-- 开头的参数）。

    struct ParseResult
    {
        bool ok{false};                                                   // 解析是否成功，成功时为 true，失败时为 false。
        bool help_requested{false};                                       // 是否请求帮助（如出现 --help），调用方可以根据此字段决定是否显示帮助文本。
        int exit_code{2};                                                 // 解析结果对应的退出代码，默认值为 2，表示解析错误。调用方可以根据需要设置为其他值，如 0 表示成功，1 表示一般错误等。
        std::optional<ParseError> error;                                  // 解析失败时的错误信息，包含错误类型、相关字段、原始输入和详细消息。
        std::unordered_map<std::string, std::vector<std::string>> values; // 解析得到的选项和值的映射，key 是选项或位置参数的名称，value 是一个字符串列表，包含所有出现的值（便于实现 RepeatMode 和 Count 语义）。
        std::vector<TraceEvent> trace;                                    // 解析 trace 事件（仅在启用 trace 时填充）。
        std::optional<SubcommandPath> subcommand_path;                    // 子命令路径（如果匹配到子命令）。

        bool Has(std::string_view key) const;                                              // 检查是否存在某个选项或位置参数。
        std::vector<std::string> GetAll(std::string_view key) const;                       // 获取某个选项或位置参数的所有值，返回一个字符串列表。
        std::string GetString(std::string_view key, std::string_view fallback = "") const; // 获取某个选项或位置参数的值并转换为字符串，如果不存在则返回 fallback。
        int GetInt(std::string_view key, int fallback = 0) const;                          // 获取某个选项或位置参数的值并转换为整数，如果不存在或转换失败则返回 fallback。
        double GetDouble(std::string_view key, double fallback = 0.0) const;               // 获取某个选项或位置参数的值并转换为双精度浮点数，如果不存在或转换失败则返回 fallback。
        bool GetBool(std::string_view key, bool fallback = false) const;                   // 获取某个选项或位置参数的值并转换为布尔值，如果不存在或转换失败则返回 fallback。
        int GetCount(std::string_view key) const;                                          // 获取某个选项或位置参数的出现次数，适用于 RepeatMode::Count 或 BoolFlagMode::Count 的选项。
    };
    // ParseResult: 存放解析结果、错误信息与解析得到的值。
    // - values: key -> list of 字符串值（便于实现 RepeatMode 与 Count 语义）

    struct MutexGroup
    {
        std::vector<std::string> option_names; // 互斥选项的名称列表。
        std::string message;                   // 错误消息，用于描述互斥选项冲突的情况。
    };
    // MutexGroup: 表示互斥选项组，超过一个激活将触发错误。

    struct DependencyRule
    {
        std::string option_name;
        std::string required_option_name;
        std::string message;
    };
    // DependencyRule: 表示选项依赖规则，例如 --foo 需要 --bar。

    class SubcommandRouter
    {
    public:
        using Handler = std::function<int(const std::vector<std::string> &)>; // 子命令处理函数类型，接受子命令参数列表，返回退出代码。

        void Register(std::string name, Handler handler, std::string description = "");         // 注册一个子命令及其处理函数。
        int Dispatch(const std::vector<std::string> &args, std::string *error = nullptr) const; // 根据子命令名称分发到对应的处理函数，args[0] 应为子命令名称，后续为子命令参数。
        std::vector<std::string> Names() const;                                                 // 获取已注册的子命令名称列表。
        std::string DescriptionFor(std::string_view name) const;                                // 获取指定子命令的描述信息。

    private:
        struct Entry
        {
            std::string name;
            Handler handler; // 子命令处理函数。
            std::string description;
        };

        std::vector<Entry> entries_; // 注册的子命令列表，按注册顺序存储以便于查找和帮助文本生成。
    };
    // SubcommandRouter: 支持子命令的注册与调度（如 git commit、git push 模式）。

    class SubcommandTree
    {
    public:
        void RegisterRoot(std::string name, std::string description = "");
        void RegisterLeaf(std::string root, std::string leaf, std::string description = "");
        std::vector<std::string> Roots() const;
        std::vector<std::string> Leaves(std::string_view root) const;
        bool HasRoot(std::string_view root) const;
        bool HasLeaf(std::string_view root, std::string_view leaf) const;
        std::string DescriptionForRoot(std::string_view root) const;
        std::string DescriptionForLeaf(std::string_view root, std::string_view leaf) const;

    private:
        struct Leaf
        {
            std::string name;
            std::string description;
        };
        struct Root
        {
            std::string name;
            std::string description;
            std::vector<Leaf> leaves;
        };

        std::vector<Root> roots_;
    };
    // SubcommandTree: 两层子命令注册与查询。

    class Parser
    {
    public:
        using UnknownOptionHandler = std::function<bool(std::string_view token, std::string *error_message)>; // 未知选项处理函数类型，接受未知选项的原始文本和一个输出错误消息的指针，返回是否成功处理（true 表示已处理，false 表示未处理且应视为错误）。

        class OptionBuilder;     // 选项构造器，提供链式 API 定义选项属性。
        class PositionalBuilder; // 位置参数构造器，提供链式 API 定义位置参数属性。

        Parser(); // 构造函数，初始化解析器实例。

        Parser &SetProgramName(std::string name);           // 设置程序名称，用于帮助文本和错误信息中显示。
        Parser &SetDescription(std::string description);    // 设置程序描述信息，用于帮助文本中说明程序的作用。
        Parser &SetUsageExample(std::string usage_example); // 设置使用示例，用于帮助文本中展示如何使用程序。
        Parser &SetLogger(IParseLogger *logger);            // 设置解析日志记录器，解析器在遇到错误或警告时会调用 logger 的回调方法。
        Parser &EnableTrace(bool value = true);             // 启用/关闭解析 trace。
        Parser &SetHelpLayout(HelpLayout layout);           // 设置默认 help 布局。
        Parser &EnableLegacyProfile(bool value = true);     // 开启兼容桥（legacy profile）。

        OptionBuilder Option(std::string long_name, char short_name = '\0'); // 定义一个选项，返回 OptionBuilder 以便链式设置选项属性。
        OptionBuilder Flag(std::string long_name, char short_name = '\0');   // 定义一个布尔 flag 选项，等同于 Option(...).BoolFlag()，返回 OptionBuilder 以便链式设置选项属性。
        PositionalBuilder Positional(std::string name);                      // 定义一个位置参数，返回 PositionalBuilder 以便链式设置参数属性。
        Parser &AddOptionTemplate(const OptionTemplate &tpl);                // 从 OptionTemplate 定义一个选项。
        Parser &AddPositionalTemplate(const PositionalTemplate &tpl);        // 从 PositionalTemplate 定义一个位置参数。

        Parser &AddMutexGroup(MutexGroup group);                              // 添加一个互斥选项组，超过一个选项被激活将触发错误。
        Parser &AddDependency(DependencyRule rule);                           // 添加一个选项依赖规则，例如 --foo 需要 --bar。
        Parser &AddConstraintRule(ConstraintRule rule);                       // 添加一个自定义约束规则（Iteration 1 规则管线入口）。
        Parser &SetGlobalConverter(ValueType type, ValueConverter converter); // 注册/覆盖全局 converter。
        Parser &SetUnknownOptionHandler(UnknownOptionHandler handler);        // 设置未知选项处理函数，当遇到未定义的选项时调用。

        Parser &AddSubcommandRoot(std::string name, std::string description = "");                   // 注册一级子命令。
        Parser &AddSubcommandLeaf(std::string root, std::string leaf, std::string description = ""); // 注册二级子命令。

        ParseResult Parse(int argc, const char *const argv[]) const;                          // 解析命令行参数，返回 ParseResult 包含解析结果、错误信息和解析得到的值。
        std::string HelpText() const;                                                         // 生成帮助文本，包含程序描述、选项说明、位置参数说明和使用示例。
        std::string HelpText(HelpLayout layout) const;                                        // 按指定布局生成帮助文本。
        std::string ResultToJson(const ParseResult &result, bool include_trace = true) const; // 将解析结果转为 JSON。

        SubcommandRouter &MutableSubcommands();          // 获取可修改的子命令路由器，以注册子命令。
        const SubcommandRouter &Subcommands() const;     // 获取子命令路由器的只读引用。
        SubcommandTree &MutableSubcommandTree();         // 获取可修改的两层子命令树。
        const SubcommandTree &GetSubcommandTree() const; // 获取两层子命令树只读引用。

    private:
        struct OptionSpec
        {
            std::string long_name;                                  // 选项的长名称（如 "verbose" 对应 --verbose）。
            char short_name{'\0'};                                  // 选项的短名称（如 'v' 对应 -v），默认为 '\0' 表示无短名称。
            std::vector<std::string> long_aliases;                  // 选项的长名称别名列表。
            std::vector<char> short_aliases;                        // 选项的短名称别名列表。
            ValueType value_type{ValueType::String};                // 选项的值类型。
            bool takes_value{true};                                 // 选项是否需要值。
            bool required{false};                                   // 选项是否为必需。
            RepeatMode repeat_mode{RepeatMode::Override};           // 重复模式。
            BoolFlagMode bool_mode{BoolFlagMode::Switch};           // 布尔标志模式。
            std::optional<std::string> default_value;               // 默认值。
            std::string value_name{"VALUE"};                        // 值的名称。
            std::string description;                                // 选项的描述信息。
            std::optional<double> min_value;                        // 最小值。
            std::optional<double> max_value;                        // 最大值。
            std::vector<std::string> choices;                       // 可选值列表。
            RangePolicy range_policy{RangePolicy::Fail};            // 范围策略。
            std::optional<ValueConverter> converter;                // 局部 converter。
            ValueCardinality cardinality{ValueCardinality::Single}; // 值基数语义。
        };

        struct PositionalSpec
        {
            std::string name;                                       // 位置参数的名称。
            ValueType value_type{ValueType::String};                // 位置参数的值类型。
            bool required{true};                                    // 位置参数是否为必需。
            bool variadic{false};                                   // 位置参数是否为可变参数。
            std::optional<std::string> default_value;               // 默认值。
            std::string description;                                // 位置参数的描述信息。
            std::optional<double> min_value;                        // 最小值。
            std::optional<double> max_value;                        // 最大值。
            std::vector<std::string> choices;                       // 可选值列表。
            RangePolicy range_policy{RangePolicy::Fail};            // 范围策略。
            std::optional<ValueConverter> converter;                // 局部 converter。
            ValueCardinality cardinality{ValueCardinality::Single}; // 值基数语义。
        };

    public:
        class OptionBuilder
        {
        public:
            OptionBuilder(Parser *parser, std::size_t index); // 构造函数，接受解析器指针和选项索引。

            OptionBuilder &String();                                                                          // 将选项值类型设置为字符串。
            OptionBuilder &Int();                                                                             // 将选项值类型设置为整数。
            OptionBuilder &Double();                                                                          // 将选项值类型设置为双精度浮点数。
            OptionBuilder &BoolFlag();                                                                        // 将选项定义为布尔 flag，默认模式为开关。
            OptionBuilder &BoolMode(BoolFlagMode mode);                                                       // 设置布尔 flag 的模式。
            OptionBuilder &Alias(std::string long_alias);                                                     // 添加一个长名称别名。
            OptionBuilder &Aliases(std::vector<std::string> long_aliases);                                    // 添加多个长名称别名。
            OptionBuilder &ShortAlias(char short_alias);                                                      // 添加一个短名称别名。
            OptionBuilder &ShortAliases(std::vector<char> short_aliases);                                     // 添加多个短名称别名。
            OptionBuilder &ConvertWith(ValueConverter converter);                                             // 为该选项设置局部 converter（优先级高于全局 converter）。
            OptionBuilder &OptionalValue(bool value = true);                                                  // 标记该选项语义为 optional（默认 required=false）。
            OptionBuilder &ListValue(bool value = true);                                                      // 标记该选项语义为 list，并自动设置 RepeatMode::Append。
            OptionBuilder &Required(bool value = true);                                                       // 设置选项是否为必需。
            OptionBuilder &Repeat(RepeatMode mode);                                                           // 设置选项的重复模式。
            OptionBuilder &Default(std::string value);                                                        // 设置选项的默认值。
            OptionBuilder &ValueName(std::string value_name);                                                 // 设置选项值的名称。
            OptionBuilder &Description(std::string description);                                              // 设置选项的描述信息。
            OptionBuilder &Range(double min_value, double max_value, RangePolicy policy = RangePolicy::Fail); // 设置数值选项的范围和超出范围时的处理策略。
            OptionBuilder &Choices(std::vector<std::string> choices);                                         // 设置选项值的允许选择列表。

            Parser &Done(); // 完成选项定义，返回解析器以继续定义其他选项或位置参数。

        private:
            Parser *parser_;    // 指向所属解析器的指针，用于在链式调用中修改解析器状态。
            std::size_t index_; // 选项在解析器中的索引，用于访问和修改对应的 OptionSpec。
        };

        class PositionalBuilder
        {
        public:
            PositionalBuilder(Parser *parser, std::size_t index); // 构造函数，接受解析器指针和位置参数索引。

            PositionalBuilder &String();                                                                          // 将位置参数值类型设置为字符串。
            PositionalBuilder &Int();                                                                             // 将位置参数值类型设置为整数。
            PositionalBuilder &Double();                                                                          // 将位置参数值类型设置为双精度浮点数。
            PositionalBuilder &ConvertWith(ValueConverter converter);                                             // 为该位置参数设置局部 converter（优先级高于全局 converter）。
            PositionalBuilder &OptionalValue(bool value = true);                                                  // 标记该位置参数语义为 optional（会设置 required=false）。
            PositionalBuilder &ListValue(bool value = true);                                                      // 标记该位置参数语义为 list（会设置 variadic=true）。
            PositionalBuilder &Required(bool value = true);                                                       // 设置位置参数是否为必需。
            PositionalBuilder &Variadic(bool value = true);                                                       // 设置位置参数是否为可变参数。
            PositionalBuilder &Default(std::string value);                                                        // 设置位置参数的默认值（仅在非可变参数时适用）。
            PositionalBuilder &Description(std::string description);                                              // 设置位置参数的描述信息。
            PositionalBuilder &Range(double min_value, double max_value, RangePolicy policy = RangePolicy::Fail); // 设置位置参数的范围和超出范围时的处理策略。
            PositionalBuilder &Choices(std::vector<std::string> choices);                                         // 设置位置参数值的允许选择列表。

            Parser &Done(); // 完成位置参数定义，返回解析器以继续定义其他选项或位置参数。

        private:
            Parser *parser_;
            std::size_t index_;
        };

    private:
        void ValidateOptionSpecOrThrow(const OptionSpec &spec, std::size_t index) const;                                            // 验证选项规范是否合法，若不合法则抛出 std::invalid_argument 异常。
        void ValidatePositionalSpecOrThrow(const PositionalSpec &spec, std::size_t index) const;                                    // 验证位置参数规范是否合法，若不合法则抛出 std::invalid_argument 异常。
        void ValidateConfigurationOrThrow() const;                                                                                  // 验证整个解析器配置是否合法（如互斥组和依赖规则），若不合法则抛出 std::invalid_argument 异常。
        ParseResult ApplyConstraintPipeline(ParseResult result) const;                                                              // 执行约束规则管线（内建规则 + 自定义规则）。
        ParseResult Fail(ParseResult result, ParseErrorKind kind, std::string field, std::string token, std::string message) const; // 构造一个失败的 ParseResult，包含错误信息。
        void Trace(ParseResult &result, std::string stage, std::string token, std::string detail) const;                            // 追加 trace 事件。

        std::string program_name_;                              // 程序名称，用于帮助文本和错误信息中显示。
        mutable std::string runtime_program_name_;              // 运行时程序名称，默认为 program_name_，但在子命令调度时会更新为实际调用的子命令名称。
        std::string description_;                               // 程序描述。
        std::string usage_example_;                             // 使用示例。
        std::vector<OptionSpec> options_;                       // 定义的选项列表，按注册顺序存储以便于查找和帮助文本生成。
        std::vector<PositionalSpec> positionals_;               // 定义的位置参数列表，按注册顺序存储以便于查找和帮助文本生成。
        std::vector<MutexGroup> mutex_groups_;                  // 定义的互斥选项组列表。
        std::vector<DependencyRule> dependency_rules_;          // 定义的选项依赖规则列表。
        std::vector<ConstraintRule> constraint_rules_;          // 自定义约束规则列表。
        std::optional<ValueConverter> global_string_converter_; // 全局 string converter。
        std::optional<ValueConverter> global_int_converter_;    // 全局 int converter。
        std::optional<ValueConverter> global_double_converter_; // 全局 double converter。
        std::optional<ValueConverter> global_bool_converter_;   // 全局 bool converter。
        UnknownOptionHandler unknown_option_handler_;           // 未知选项处理函数。
        IParseLogger *logger_{nullptr};                         // 解析日志记录器，解析器在遇到错误或警告时会调用 logger 的回调方法。
        SubcommandRouter subcommands_;                          // 子命令路由器，用于注册和调度子命令。
        SubcommandTree subcommand_tree_;                        // 两层子命令树。
        bool trace_enabled_{false};                             // 是否启用解析 trace。
        HelpLayout help_layout_{HelpLayout::Fixed};             // 默认 help 布局。
        bool legacy_profile_enabled_{false};                    // 兼容桥是否启用。
    };

} // namespace argtool
