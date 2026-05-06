#include "cfgx.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#ifdef _WIN32
extern "C" __declspec(dllimport) char *__stdcall GetEnvironmentStringsA(void);  // Windows API函数，获取环境变量字符串，返回指向环境变量块的指针，每个环境变量以null结尾，整个块以双null结尾
extern "C" __declspec(dllimport) int __stdcall FreeEnvironmentStringsA(char *); // Windows API函数，释放由GetEnvironmentStringsA返回的环境变量块，参数是指向环境变量块的指针，返回非零表示成功，零表示失败
#endif

namespace cfgx
{

    namespace
    {

        Status OkStatus()
        {
            return Status{true, ""};
        }

        Status FailStatus(std::string message)
        {
            return Status{false, std::move(message)};
        }

        std::string ToLowerAscii(std::string_view input)
        {
            std::string out(input);
            std::transform(out.begin(), out.end(), out.begin(), [](unsigned char ch)
                           { return static_cast<char>(std::tolower(ch)); });
            return out;
        }

        std::string TrimCopy(std::string_view input)
        {
            std::size_t begin = 0;
            while (begin < input.size() && std::isspace(static_cast<unsigned char>(input[begin])) != 0)
            {
                ++begin;
            }

            std::size_t end = input.size();
            while (end > begin && std::isspace(static_cast<unsigned char>(input[end - 1])) != 0)
            {
                --end;
            }

            return std::string(input.substr(begin, end - begin));
        }

        bool StartsWith(std::string_view text, std::string_view prefix)
        {
            return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
        }

        std::string StripYamlComment(std::string_view line)
        {
            bool in_single_quote = false;
            bool in_double_quote = false;

            for (std::size_t i = 0; i < line.size(); ++i)
            {
                const char ch = line[i];
                if (ch == '\'' && !in_double_quote)
                {
                    in_single_quote = !in_single_quote;
                    continue;
                }
                if (ch == '"' && !in_single_quote)
                {
                    bool escaped = false;
                    std::size_t back = i;
                    while (back > 0 && line[back - 1] == '\\')
                    {
                        escaped = !escaped;
                        --back;
                    }
                    if (!escaped)
                    {
                        in_double_quote = !in_double_quote;
                    }
                    continue;
                }
                if (ch == '#' && !in_single_quote && !in_double_quote)
                {
                    return TrimCopy(line.substr(0, i));
                }
            }
            return TrimCopy(line);
        }

        Node ParseScalar(std::string_view raw, bool strict_string = false)
        {
            const std::string text = TrimCopy(raw);
            if (text.empty())
            {
                return Node("");
            }

            if (strict_string)
            {
                return Node(text);
            }

            if ((text.size() >= 2 && text.front() == '"' && text.back() == '"') ||
                (text.size() >= 2 && text.front() == '\'' && text.back() == '\''))
            {
                return Node(text.substr(1, text.size() - 2));
            }

            const std::string lower = ToLowerAscii(text);
            if (lower == "null" || lower == "~")
            {
                return Node();
            }
            if (lower == "true" || lower == "yes" || lower == "on")
            {
                return Node(true);
            }
            if (lower == "false" || lower == "no" || lower == "off")
            {
                return Node(false);
            }

            std::int64_t int_value = 0;
            {
                const auto *begin = text.data();
                const auto *end = text.data() + text.size();
                const auto parsed = std::from_chars(begin, end, int_value);
                if (parsed.ec == std::errc() && parsed.ptr == end)
                {
                    return Node(int_value);
                }
            }

            char *parse_end = nullptr;
            const double double_value = std::strtod(text.c_str(), &parse_end);
            if (parse_end != nullptr && *parse_end == '\0')
            {
                const bool finite = (double_value <= std::numeric_limits<double>::max() &&
                                     double_value >= -std::numeric_limits<double>::max());
                if (finite)
                {
                    return Node(double_value);
                }
            }

            return Node(text);
        }

        bool ResolveStrictStringPolicy(const TypePolicyOptions &policy,
                                       SourceLayer layer,
                                       std::string_view path)
        {
            for (const auto &entry : policy.strict_string_by_path)
            {
                if (entry.first == path)
                {
                    return entry.second;
                }
            }

            for (const auto &entry : policy.strict_string_by_source)
            {
                if (entry.first == layer)
                {
                    return entry.second;
                }
            }

            return policy.strict_string_global;
        }

        std::optional<std::string> ParseEnvPath(std::string_view key,
                                                std::string_view prefix,
                                                std::string &error)
        {
            if (!StartsWith(key, prefix))
            {
                return std::nullopt;
            }

            const std::string_view tail = key.substr(prefix.size());
            if (tail.empty())
            {
                error = "env key uses prefix but has empty mapped path: " + std::string(key);
                return std::nullopt;
            }

            std::vector<std::string> segments;
            std::size_t start = 0;
            while (start <= tail.size())
            {
                const std::size_t split = tail.find("__", start);
                const std::size_t end = (split == std::string_view::npos) ? tail.size() : split;
                std::string segment = ToLowerAscii(tail.substr(start, end - start));
                if (segment.empty())
                {
                    error = "env key contains empty segment: " + std::string(key);
                    return std::nullopt;
                }

                for (char ch : segment)
                {
                    const bool ok = (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_';
                    if (!ok)
                    {
                        error = "env key contains unsupported character for mapping: " + std::string(key);
                        return std::nullopt;
                    }
                }

                segments.push_back(std::move(segment));
                if (split == std::string_view::npos)
                {
                    break;
                }
                start = split + 2;
            }

            if (segments.empty())
            {
                error = "env key did not produce path segments: " + std::string(key);
                return std::nullopt;
            }

            std::ostringstream oss;
            for (std::size_t i = 0; i < segments.size(); ++i)
            {
                if (i > 0)
                {
                    oss << '.';
                }
                oss << segments[i];
            }
            return oss.str();
        }

        bool IsMissingPathError(std::string_view message)
        {
            return message.find("not found") != std::string_view::npos ||
                   message.find("out of range") != std::string_view::npos;
        }

        std::optional<std::size_t> FindObjectIndex(const Node::Object &obj, std::string_view key);

        std::vector<std::pair<std::string, std::string>> CollectEnvironmentPairs()
        {
            std::vector<std::pair<std::string, std::string>> out;

#ifdef _WIN32
            char *block = GetEnvironmentStringsA();
            if (block == nullptr)
            {
                return out;
            }

            const char *cursor = block;
            while (*cursor != '\0')
            {
                const std::string entry(cursor);
                cursor += entry.size() + 1;

                if (entry.empty() || entry[0] == '=')
                {
                    continue;
                }

                const std::size_t split = entry.find('=');
                if (split == std::string::npos)
                {
                    continue;
                }

                out.push_back({entry.substr(0, split), entry.substr(split + 1)});
            }

            FreeEnvironmentStringsA(block);
#else
            extern char **environ;
            if (environ == nullptr)
            {
                return out;
            }

            for (char **cursor = environ; *cursor != nullptr; ++cursor)
            {
                const std::string entry(*cursor);
                const std::size_t split = entry.find('=');
                if (split == std::string::npos)
                {
                    continue;
                }
                out.push_back({entry.substr(0, split), entry.substr(split + 1)});
            }
#endif

            return out;
        }

        std::string NormalizeAdapterKey(std::string_view name)
        {
            return ToLowerAscii(TrimCopy(name));
        }

        std::mutex &AdapterRegistryMutex()
        {
            static std::mutex mutex;
            return mutex;
        }

        std::unordered_map<std::string, ParserAdapter> &AdapterRegistry()
        {
            static std::unordered_map<std::string, ParserAdapter> registry;
            return registry;
        }

        std::string &ActiveAdapterKey()
        {
            static std::string key;
            return key;
        }

        std::mutex &RemoteFetcherMutex()
        {
            static std::mutex mutex;
            return mutex;
        }

        RemoteFetcher &RemoteFetcherState()
        {
            static RemoteFetcher fetcher;
            return fetcher;
        }

        RemoteFetcher AcquireRemoteFetcherCopy()
        {
            std::lock_guard<std::mutex> lock(RemoteFetcherMutex());
            return RemoteFetcherState();
        }

        std::string StripUrlQueryAndFragment(std::string_view url)
        {
            const auto query_pos = url.find('?');
            const auto frag_pos = url.find('#');
            std::size_t end = url.size();
            if (query_pos != std::string_view::npos && query_pos < end)
            {
                end = query_pos;
            }
            if (frag_pos != std::string_view::npos && frag_pos < end)
            {
                end = frag_pos;
            }
            return std::string(url.substr(0, end));
        }

        bool AcquireActiveAdapter(std::function<Result<Node>(std::string_view, ConfigFormat)> *parse,
                                  std::function<Result<std::string>(const Node &, ConfigFormat, int)> *dump,
                                  std::string *name)
        {
            std::lock_guard<std::mutex> lock(AdapterRegistryMutex());
            const std::string key = ActiveAdapterKey();
            if (key.empty())
            {
                return false;
            }

            const auto it = AdapterRegistry().find(key);
            if (it == AdapterRegistry().end())
            {
                return false;
            }

            if (parse != nullptr)
            {
                *parse = it->second.parse;
            }
            if (dump != nullptr)
            {
                *dump = it->second.dump;
            }
            if (name != nullptr)
            {
                *name = it->second.name;
            }
            return true;
        }

        bool IsScalarNode(const Node &node)
        {
            const auto kind = node.Kind();
            return kind == NodeKind::Null || kind == NodeKind::Bool || kind == NodeKind::Integer ||
                   kind == NodeKind::Double || kind == NodeKind::String;
        }

        bool IsIntegralDouble(double value, std::int64_t &out) noexcept
        {
            if (!std::isfinite(value))
            {
                return false;
            }

            double integral = 0.0;
            if (std::modf(value, &integral) != 0.0)
            {
                return false;
            }

            if (integral < static_cast<double>(std::numeric_limits<std::int64_t>::min()) ||
                integral > static_cast<double>(std::numeric_limits<std::int64_t>::max()))
            {
                return false;
            }

            out = static_cast<std::int64_t>(integral);
            return true;
        }

        struct FileFingerprint
        {
            bool exists{false};
            std::uint64_t mtime{0};
            std::uint64_t content_hash{0};
        };

        std::uint64_t Fnv1a64(std::string_view text)
        {
            constexpr std::uint64_t kOffset = 1469598103934665603ULL;
            constexpr std::uint64_t kPrime = 1099511628211ULL;

            std::uint64_t hash = kOffset;
            for (unsigned char ch : text)
            {
                hash ^= static_cast<std::uint64_t>(ch);
                hash *= kPrime;
            }
            return hash;
        }

        std::uint64_t SteadyNowMs()
        {
            using namespace std::chrono;
            return static_cast<std::uint64_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
        }

        Result<FileFingerprint> CaptureFileFingerprint(std::string_view path, bool allow_missing)
        {
            FileFingerprint fp;
            if (path.empty())
            {
                return Result<FileFingerprint>{true, fp, ""};
            }

            std::error_code ec;
            const auto fs_path = std::filesystem::path(std::string(path));
            const bool exists = std::filesystem::exists(fs_path, ec);
            if (ec)
            {
                return Result<FileFingerprint>{false, {}, "filesystem exists() failed for '" + std::string(path) + "': " + ec.message()};
            }

            if (!exists)
            {
                if (!allow_missing)
                {
                    return Result<FileFingerprint>{false, {}, "required config file not found: " + std::string(path)};
                }
                return Result<FileFingerprint>{true, fp, ""};
            }

            std::ifstream input(std::string(path), std::ios::binary);
            if (!input.is_open())
            {
                return Result<FileFingerprint>{false, {}, "failed to open file for fingerprint: " + std::string(path)};
            }

            std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
            fp.exists = true;
            fp.content_hash = Fnv1a64(text);

            const auto file_time = std::filesystem::last_write_time(fs_path, ec);
            if (ec)
            {
                return Result<FileFingerprint>{false, {}, "last_write_time failed for '" + std::string(path) + "': " + ec.message()};
            }
            fp.mtime = static_cast<std::uint64_t>(file_time.time_since_epoch().count());
            return Result<FileFingerprint>{true, fp, ""};
        }

        bool NodeEquals(const Node &lhs, const Node &rhs)
        {
            if (lhs.Kind() != rhs.Kind())
            {
                return false;
            }

            switch (lhs.Kind())
            {
            case NodeKind::Null:
                return true;
            case NodeKind::Bool:
                return lhs.AsBool() == rhs.AsBool();
            case NodeKind::Integer:
                return lhs.AsInt() == rhs.AsInt();
            case NodeKind::Double:
                return lhs.AsDouble() == rhs.AsDouble();
            case NodeKind::String:
                return lhs.AsString() == rhs.AsString();
            case NodeKind::Object:
            {
                const auto *left_obj = lhs.TryObject();
                const auto *right_obj = rhs.TryObject();
                if (left_obj == nullptr || right_obj == nullptr)
                {
                    return false;
                }
                if (left_obj->size() != right_obj->size())
                {
                    return false;
                }

                for (const auto &kv : *left_obj)
                {
                    const auto index = FindObjectIndex(*right_obj, kv.first);
                    if (!index.has_value())
                    {
                        return false;
                    }
                    if (!NodeEquals(kv.second, (*right_obj)[*index].second))
                    {
                        return false;
                    }
                }
                return true;
            }
            case NodeKind::Array:
            {
                const auto *left_arr = lhs.TryArray();
                const auto *right_arr = rhs.TryArray();
                if (left_arr == nullptr || right_arr == nullptr)
                {
                    return false;
                }
                if (left_arr->size() != right_arr->size())
                {
                    return false;
                }
                for (std::size_t i = 0; i < left_arr->size(); ++i)
                {
                    if (!NodeEquals((*left_arr)[i], (*right_arr)[i]))
                    {
                        return false;
                    }
                }
                return true;
            }
            }

            return false;
        }

        void FlattenLeafNodes(const Node &node,
                              const std::string &path,
                              std::map<std::string, Node> &out)
        {
            if (node.IsObject())
            {
                const auto *obj = node.TryObject();
                if (obj != nullptr)
                {
                    if (obj->empty())
                    {
                        out[path.empty() ? "value" : path] = node;
                        return;
                    }

                    for (const auto &kv : *obj)
                    {
                        const std::string next = path.empty() ? kv.first : (path + "." + kv.first);
                        FlattenLeafNodes(kv.second, next, out);
                    }
                }
                return;
            }

            if (node.IsArray())
            {
                const auto *arr = node.TryArray();
                if (arr != nullptr)
                {
                    if (arr->empty())
                    {
                        out[path.empty() ? "value" : path] = node;
                        return;
                    }

                    for (std::size_t i = 0; i < arr->size(); ++i)
                    {
                        const std::string next = path + "[" + std::to_string(i) + "]";
                        FlattenLeafNodes((*arr)[i], next, out);
                    }
                }
                return;
            }

            out[path.empty() ? "value" : path] = node;
        }

        std::vector<DiffEntry> BuildDiffEntries(const Node *old_root,
                                                const Node &new_root,
                                                const std::vector<SourceAttribution> &trace)
        {
            std::map<std::string, Node> old_flat;
            std::map<std::string, Node> new_flat;
            if (old_root != nullptr)
            {
                FlattenLeafNodes(*old_root, "", old_flat);
            }
            FlattenLeafNodes(new_root, "", new_flat);

            std::unordered_map<std::string, SourceLayer> trace_map;
            trace_map.reserve(trace.size() * 2 + 1);
            for (const auto &entry : trace)
            {
                trace_map[entry.path] = entry.layer;
            }

            std::vector<DiffEntry> diff;
            for (const auto &entry : new_flat)
            {
                const auto old_it = old_flat.find(entry.first);
                if (old_it == old_flat.end())
                {
                    const auto layer_it = trace_map.find(entry.first);
                    diff.push_back(DiffEntry{entry.first, DiffKind::Added, layer_it == trace_map.end() ? SourceLayer::Base : layer_it->second});
                    continue;
                }

                if (!NodeEquals(old_it->second, entry.second))
                {
                    const auto layer_it = trace_map.find(entry.first);
                    diff.push_back(DiffEntry{entry.first, DiffKind::Changed, layer_it == trace_map.end() ? SourceLayer::Base : layer_it->second});
                }
            }

            for (const auto &entry : old_flat)
            {
                if (new_flat.find(entry.first) != new_flat.end())
                {
                    continue;
                }
                diff.push_back(DiffEntry{entry.first, DiffKind::Removed, SourceLayer::Base});
            }

            std::sort(diff.begin(), diff.end(), [](const DiffEntry &lhs, const DiffEntry &rhs)
                      { return lhs.path < rhs.path; });
            return diff;
        }

        std::string ScalarToIniText(const Node &node)
        {
            switch (node.Kind())
            {
            case NodeKind::Null:
                return "null";
            case NodeKind::Bool:
                return node.AsBool() ? "true" : "false";
            case NodeKind::Integer:
                return std::to_string(node.AsInt());
            case NodeKind::Double:
            {
                std::ostringstream oss;
                oss << node.AsDouble();
                return oss.str();
            }
            case NodeKind::String:
                return node.AsString();
            default:
                return ToJson(node, 0);
            }
        }

        std::string ScalarToYamlText(const Node &node)
        {
            switch (node.Kind())
            {
            case NodeKind::Null:
                return "null";
            case NodeKind::Bool:
                return node.AsBool() ? "true" : "false";
            case NodeKind::Integer:
                return std::to_string(node.AsInt());
            case NodeKind::Double:
            {
                std::ostringstream oss;
                oss << node.AsDouble();
                return oss.str();
            }
            case NodeKind::String:
            {
                const std::string value = node.AsString();
                std::string escaped;
                escaped.reserve(value.size() + 8);
                for (char ch : value)
                {
                    if (ch == '\\' || ch == '"')
                    {
                        escaped.push_back('\\');
                    }
                    escaped.push_back(ch);
                }
                return std::string("\"") + escaped + "\"";
            }
            default:
                return ToJson(node, 0);
            }
        }

        std::optional<std::size_t> FindObjectIndex(const Node::Object &obj, std::string_view key)
        {
            for (std::size_t i = 0; i < obj.size(); ++i)
            {
                if (obj[i].first == key)
                {
                    return i;
                }
            }
            return std::nullopt;
        }

        std::string EscapeJsonString(std::string_view text)
        {
            std::string out;
            out.reserve(text.size() + 8);
            for (char c : text)
            {
                switch (c)
                {
                case '"':
                    out += "\\\"";
                    break;
                case '\\':
                    out += "\\\\";
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
                case '\b':
                    out += "\\b";
                    break;
                case '\f':
                    out += "\\f";
                    break;
                default:
                    out += c;
                    break;
                }
            }
            return out;
        }

        std::string PathError(std::size_t offset, std::string_view reason)
        {
            std::ostringstream oss;
            oss << "invalid path at offset " << offset << ": " << reason;
            return oss.str();
        }

        class JsonParser
        {
        public:
            explicit JsonParser(std::string_view text)
                : text_(text)
            {
            }

            Result<Node> Parse()
            {
                SkipWhitespace();
                auto value = ParseValue();
                if (!value.ok)
                {
                    return value;
                }

                SkipWhitespace();
                if (!Eof())
                {
                    return Fail("unexpected trailing content");
                }
                return value;
            }

        private:
            std::string_view text_;
            std::size_t pos_{0};
            int line_{1};
            int column_{1};

            bool Eof() const noexcept
            {
                return pos_ >= text_.size();
            }

            char Peek() const noexcept
            {
                return Eof() ? '\0' : text_[pos_];
            }

            char Advance()
            {
                if (Eof())
                {
                    return '\0';
                }
                const char ch = text_[pos_++];
                if (ch == '\n')
                {
                    ++line_;
                    column_ = 1;
                }
                else
                {
                    ++column_;
                }
                return ch;
            }

            void SkipWhitespace()
            {
                while (!Eof())
                {
                    const unsigned char ch = static_cast<unsigned char>(Peek());
                    if (std::isspace(ch) == 0)
                    {
                        break;
                    }
                    Advance();
                }
            }

            Result<Node> Fail(std::string_view message)
            {
                std::ostringstream oss;
                oss << message << " at line " << line_ << ", column " << column_;
                return Result<Node>{false, Node{}, oss.str()};
            }

            Result<std::string> ParseStringLiteral()
            {
                if (Peek() != '"')
                {
                    return Result<std::string>{false, "", "expected string literal"};
                }

                Advance();
                std::string out;
                while (!Eof())
                {
                    const char ch = Advance();
                    if (ch == '"')
                    {
                        return Result<std::string>{true, std::move(out), ""};
                    }

                    if (ch != '\\')
                    {
                        out += ch;
                        continue;
                    }

                    if (Eof())
                    {
                        return Result<std::string>{false, "", "unterminated escape"};
                    }

                    const char esc = Advance();
                    switch (esc)
                    {
                    case '"':
                        out += '"';
                        break;
                    case '\\':
                        out += '\\';
                        break;
                    case '/':
                        out += '/';
                        break;
                    case 'b':
                        out += '\b';
                        break;
                    case 'f':
                        out += '\f';
                        break;
                    case 'n':
                        out += '\n';
                        break;
                    case 'r':
                        out += '\r';
                        break;
                    case 't':
                        out += '\t';
                        break;
                    case 'u':
                    {
                        if (pos_ + 4 > text_.size())
                        {
                            return Result<std::string>{false, "", "invalid unicode escape"};
                        }

                        unsigned int code = 0;
                        for (int i = 0; i < 4; ++i)
                        {
                            const char hc = text_[pos_ + i];
                            code <<= 4;
                            if (hc >= '0' && hc <= '9')
                            {
                                code += static_cast<unsigned int>(hc - '0');
                            }
                            else if (hc >= 'a' && hc <= 'f')
                            {
                                code += static_cast<unsigned int>(10 + hc - 'a');
                            }
                            else if (hc >= 'A' && hc <= 'F')
                            {
                                code += static_cast<unsigned int>(10 + hc - 'A');
                            }
                            else
                            {
                                return Result<std::string>{false, "", "invalid unicode escape"};
                            }
                        }

                        // Keep V1 simple: ASCII unicode escapes are decoded, others fallback to '?'.
                        if (code <= 0x7F)
                        {
                            out += static_cast<char>(code);
                        }
                        else
                        {
                            out += '?';
                        }
                        for (int i = 0; i < 4; ++i)
                        {
                            Advance();
                        }
                        break;
                    }
                    default:
                        return Result<std::string>{false, "", "unsupported escape sequence"};
                    }
                }

                return Result<std::string>{false, "", "unterminated string literal"};
            }

            Result<Node> ParseNumber()
            {
                const std::size_t start = pos_;
                while (!Eof())
                {
                    const char ch = Peek();
                    if ((ch >= '0' && ch <= '9') || ch == '-' || ch == '+' || ch == '.' || ch == 'e' || ch == 'E')
                    {
                        Advance();
                        continue;
                    }
                    break;
                }

                const std::string token(text_.substr(start, pos_ - start));
                if (token.empty())
                {
                    return Fail("invalid number");
                }

                if (token.find('.') == std::string::npos && token.find('e') == std::string::npos && token.find('E') == std::string::npos)
                {
                    std::int64_t int_value = 0;
                    const auto *begin = token.data();
                    const auto *end = token.data() + token.size();
                    const auto parse_result = std::from_chars(begin, end, int_value);
                    if (parse_result.ec == std::errc() && parse_result.ptr == end)
                    {
                        return Result<Node>{true, Node(int_value), ""};
                    }
                    return Fail("invalid integer literal");
                }

                char *parse_end = nullptr;
                const double parsed = std::strtod(token.c_str(), &parse_end);
                if (parse_end == nullptr || *parse_end != '\0')
                {
                    return Fail("invalid numeric literal");
                }
                return Result<Node>{true, Node(parsed), ""};
            }

            Result<Node> ParseArray()
            {
                if (Peek() != '[')
                {
                    return Fail("expected '['");
                }
                Advance();

                Node::Array arr;
                SkipWhitespace();
                if (Peek() == ']')
                {
                    Advance();
                    return Result<Node>{true, Node(std::move(arr)), ""};
                }

                while (!Eof())
                {
                    SkipWhitespace();
                    auto value = ParseValue();
                    if (!value.ok)
                    {
                        return value;
                    }
                    arr.push_back(std::move(value.value));

                    SkipWhitespace();
                    if (Peek() == ',')
                    {
                        Advance();
                        continue;
                    }
                    if (Peek() == ']')
                    {
                        Advance();
                        return Result<Node>{true, Node(std::move(arr)), ""};
                    }
                    return Fail("expected ',' or ']' in array");
                }

                return Fail("unterminated array");
            }

            Result<Node> ParseObject()
            {
                if (Peek() != '{')
                {
                    return Fail("expected '{'");
                }
                Advance();

                Node::Object obj;
                SkipWhitespace();
                if (Peek() == '}')
                {
                    Advance();
                    return Result<Node>{true, Node(std::move(obj)), ""};
                }

                while (!Eof())
                {
                    SkipWhitespace();
                    auto key = ParseStringLiteral();
                    if (!key.ok)
                    {
                        return Fail(key.error);
                    }

                    SkipWhitespace();
                    if (Peek() != ':')
                    {
                        return Fail("expected ':' after object key");
                    }
                    Advance();

                    SkipWhitespace();
                    auto value = ParseValue();
                    if (!value.ok)
                    {
                        return value;
                    }

                    obj.push_back({std::move(key.value), std::move(value.value)});

                    SkipWhitespace();
                    if (Peek() == ',')
                    {
                        Advance();
                        continue;
                    }
                    if (Peek() == '}')
                    {
                        Advance();
                        return Result<Node>{true, Node(std::move(obj)), ""};
                    }
                    return Fail("expected ',' or '}' in object");
                }

                return Fail("unterminated object");
            }

            Result<Node> ParseValue()
            {
                SkipWhitespace();
                if (Eof())
                {
                    return Fail("unexpected end of input");
                }

                const char ch = Peek();
                if (ch == '{')
                {
                    return ParseObject();
                }
                if (ch == '[')
                {
                    return ParseArray();
                }
                if (ch == '"')
                {
                    auto s = ParseStringLiteral();
                    if (!s.ok)
                    {
                        return Fail(s.error);
                    }
                    return Result<Node>{true, Node(std::move(s.value)), ""};
                }
                if (ch == 't')
                {
                    if (text_.substr(pos_, 4) == "true")
                    {
                        for (int i = 0; i < 4; ++i)
                        {
                            Advance();
                        }
                        return Result<Node>{true, Node(true), ""};
                    }
                    return Fail("invalid token, expected 'true'");
                }
                if (ch == 'f')
                {
                    if (text_.substr(pos_, 5) == "false")
                    {
                        for (int i = 0; i < 5; ++i)
                        {
                            Advance();
                        }
                        return Result<Node>{true, Node(false), ""};
                    }
                    return Fail("invalid token, expected 'false'");
                }
                if (ch == 'n')
                {
                    if (text_.substr(pos_, 4) == "null")
                    {
                        for (int i = 0; i < 4; ++i)
                        {
                            Advance();
                        }
                        return Result<Node>{true, Node(), ""};
                    }
                    return Fail("invalid token, expected 'null'");
                }

                return ParseNumber();
            }
        };

        void AppendJson(const Node &node, std::ostringstream &oss, int indent, int depth)
        {
            const auto pad = [&](int d)
            {
                for (int i = 0; i < d * indent; ++i)
                {
                    oss << ' ';
                }
            };

            const auto kind = node.Kind();
            switch (kind)
            {
            case NodeKind::Null:
                oss << "null";
                return;
            case NodeKind::Bool:
                oss << (node.AsBool() ? "true" : "false");
                return;
            case NodeKind::Integer:
                oss << node.AsInt();
                return;
            case NodeKind::Double:
                oss << node.AsDouble();
                return;
            case NodeKind::String:
                oss << '"' << EscapeJsonString(node.AsString()) << '"';
                return;
            case NodeKind::Array:
            {
                const auto *arr = node.TryArray();
                if (arr == nullptr)
                {
                    oss << "[]";
                    return;
                }
                oss << '[';
                if (!arr->empty())
                {
                    for (std::size_t i = 0; i < arr->size(); ++i)
                    {
                        if (indent > 0)
                        {
                            oss << '\n';
                            pad(depth + 1);
                        }
                        AppendJson((*arr)[i], oss, indent, depth + 1);
                        if (i + 1 < arr->size())
                        {
                            oss << ',';
                        }
                    }
                    if (indent > 0)
                    {
                        oss << '\n';
                        pad(depth);
                    }
                }
                oss << ']';
                return;
            }
            case NodeKind::Object:
            {
                const auto *obj = node.TryObject();
                if (obj == nullptr)
                {
                    oss << "{}";
                    return;
                }
                oss << '{';
                if (!obj->empty())
                {
                    for (std::size_t i = 0; i < obj->size(); ++i)
                    {
                        if (indent > 0)
                        {
                            oss << '\n';
                            pad(depth + 1);
                        }
                        oss << '"' << EscapeJsonString((*obj)[i].first) << '"' << ':';
                        if (indent > 0)
                        {
                            oss << ' ';
                        }
                        AppendJson((*obj)[i].second, oss, indent, depth + 1);
                        if (i + 1 < obj->size())
                        {
                            oss << ',';
                        }
                    }
                    if (indent > 0)
                    {
                        oss << '\n';
                        pad(depth);
                    }
                }
                oss << '}';
                return;
            }
            }
        }

        Status MergeInternal(Node &base, const Node &overlay, bool append_arrays)
        {
            if (base.Kind() == NodeKind::Object && overlay.Kind() == NodeKind::Object)
            {
                auto *dst = base.TryObject();
                const auto *src = overlay.TryObject();
                if (dst == nullptr || src == nullptr)
                {
                    return FailStatus("internal object merge error");
                }

                for (const auto &entry : *src)
                {
                    const auto index = FindObjectIndex(*dst, entry.first);
                    if (!index.has_value())
                    {
                        dst->push_back(entry);
                        continue;
                    }

                    auto st = MergeInternal((*dst)[*index].second, entry.second, append_arrays);
                    if (!st.ok)
                    {
                        return st;
                    }
                }
                return OkStatus();
            }

            if (base.Kind() == NodeKind::Array && overlay.Kind() == NodeKind::Array)
            {
                auto *dst = base.TryArray();
                const auto *src = overlay.TryArray();
                if (dst == nullptr || src == nullptr)
                {
                    return FailStatus("internal array merge error");
                }

                if (append_arrays)
                {
                    dst->insert(dst->end(), src->begin(), src->end());
                }
                else
                {
                    *dst = *src;
                }
                return OkStatus();
            }

            base = overlay;
            return OkStatus();
        }

    } // namespace

    Node::Node()
        : data_(std::monostate{})
    {
    }

    Node::Node(bool value)
        : data_(value)
    {
    }

    Node::Node(std::int64_t value)
        : data_(value)
    {
    }

    Node::Node(double value)
        : data_(value)
    {
    }

    Node::Node(std::string value)
        : data_(std::move(value))
    {
    }

    Node::Node(const char *value)
        : data_(std::string(value != nullptr ? value : ""))
    {
    }

    Node::Node(Object value)
        : data_(std::move(value))
    {
    }

    Node::Node(Array value)
        : data_(std::move(value))
    {
    }

    Node Node::MakeObject()
    {
        return Node(Object{});
    }

    Node Node::MakeArray()
    {
        return Node(Array{});
    }

    NodeKind Node::Kind() const noexcept
    {
        switch (data_.index())
        {
        case 0:
            return NodeKind::Null;
        case 1:
            return NodeKind::Bool;
        case 2:
            return NodeKind::Integer;
        case 3:
            return NodeKind::Double;
        case 4:
            return NodeKind::String;
        case 5:
            return NodeKind::Object;
        case 6:
            return NodeKind::Array;
        default:
            return NodeKind::Null;
        }
    }

    bool Node::IsNull() const noexcept
    {
        return std::holds_alternative<std::monostate>(data_);
    }

    bool Node::IsEmpty() const noexcept
    {
        if (IsNull())
        {
            return true;
        }

        if (const auto *text = std::get_if<std::string>(&data_); text != nullptr)
        {
            return text->empty();
        }

        if (const auto *obj = std::get_if<Object>(&data_); obj != nullptr)
        {
            return obj->empty();
        }

        if (const auto *arr = std::get_if<Array>(&data_); arr != nullptr)
        {
            return arr->empty();
        }

        return false;
    }

    bool Node::IsObject() const noexcept
    {
        return std::holds_alternative<Object>(data_);
    }

    bool Node::IsArray() const noexcept
    {
        return std::holds_alternative<Array>(data_);
    }

    bool Node::IsScalar() const noexcept
    {
        return !IsObject() && !IsArray();
    }

    std::size_t Node::Size() const noexcept
    {
        if (const auto *obj = std::get_if<Object>(&data_); obj != nullptr)
        {
            return obj->size();
        }
        if (const auto *arr = std::get_if<Array>(&data_); arr != nullptr)
        {
            return arr->size();
        }
        if (const auto *text = std::get_if<std::string>(&data_); text != nullptr)
        {
            return text->size();
        }
        return 0;
    }

    bool Node::AsBool(bool fallback) const noexcept
    {
        const auto *value = std::get_if<bool>(&data_);
        return value == nullptr ? fallback : *value;
    }

    std::int64_t Node::AsInt(std::int64_t fallback) const noexcept
    {
        if (const auto *value = std::get_if<std::int64_t>(&data_); value != nullptr)
        {
            return *value;
        }

        if (const auto *value_double = std::get_if<double>(&data_); value_double != nullptr)
        {
            std::int64_t converted = 0;
            if (IsIntegralDouble(*value_double, converted))
            {
                return converted;
            }
        }

        return fallback;
    }

    double Node::AsDouble(double fallback) const noexcept
    {
        if (const auto *value = std::get_if<double>(&data_); value != nullptr)
        {
            return *value;
        }
        if (const auto *value_int = std::get_if<std::int64_t>(&data_); value_int != nullptr)
        {
            return static_cast<double>(*value_int);
        }
        return fallback;
    }

    std::string Node::AsString(std::string_view fallback) const
    {
        const auto *value = std::get_if<std::string>(&data_);
        return value == nullptr ? std::string(fallback) : *value;
    }

    const Node *Node::Get(std::string_view key) const noexcept
    {
        const auto *obj = TryObject();
        if (obj == nullptr)
        {
            return nullptr;
        }

        const auto index = FindObjectIndex(*obj, key);
        if (!index.has_value())
        {
            return nullptr;
        }
        return &(*obj)[*index].second;
    }

    Node *Node::Get(std::string_view key) noexcept
    {
        return const_cast<Node *>(static_cast<const Node &>(*this).Get(key));
    }

    Status Node::Set(std::string key, Node value)
    {
        auto *obj = TryObject();
        if (obj == nullptr)
        {
            return FailStatus("type mismatch: expected object for Set(key, value)");
        }

        const auto index = FindObjectIndex(*obj, key);
        if (index.has_value())
        {
            (*obj)[*index].second = std::move(value);
            return OkStatus();
        }

        obj->push_back({std::move(key), std::move(value)});
        return OkStatus();
    }

    Status Node::Erase(std::string_view key)
    {
        auto *obj = TryObject();
        if (obj == nullptr)
        {
            return FailStatus("type mismatch: expected object for Erase(key)");
        }

        const auto index = FindObjectIndex(*obj, key);
        if (!index.has_value())
        {
            return FailStatus("key not found: '" + std::string(key) + "'");
        }

        obj->erase(obj->begin() + static_cast<std::ptrdiff_t>(*index));
        return OkStatus();
    }

    const Node *Node::At(std::size_t index) const noexcept
    {
        const auto *arr = TryArray();
        if (arr == nullptr || index >= arr->size())
        {
            return nullptr;
        }
        return &(*arr)[index];
    }

    Node *Node::At(std::size_t index) noexcept
    {
        return const_cast<Node *>(static_cast<const Node &>(*this).At(index));
    }

    Status Node::Push(Node value)
    {
        auto *arr = TryArray();
        if (arr == nullptr)
        {
            return FailStatus("type mismatch: expected array for Push(value)");
        }

        arr->push_back(std::move(value));
        return OkStatus();
    }

    Status Node::SetAt(std::size_t index, Node value, bool auto_expand)
    {
        auto *arr = TryArray();
        if (arr == nullptr)
        {
            return FailStatus("type mismatch: expected array for SetAt(index, value)");
        }

        if (index >= arr->size())
        {
            if (!auto_expand)
            {
                return FailStatus("array index out of range");
            }
            arr->resize(index + 1);
        }

        (*arr)[index] = std::move(value);
        return OkStatus();
    }

    Status Node::EraseAt(std::size_t index)
    {
        auto *arr = TryArray();
        if (arr == nullptr)
        {
            return FailStatus("type mismatch: expected array for EraseAt(index)");
        }
        if (index >= arr->size())
        {
            return FailStatus("array index out of range");
        }

        arr->erase(arr->begin() + static_cast<std::ptrdiff_t>(index));
        return OkStatus();
    }

    const Node::Object *Node::TryObject() const noexcept
    {
        return std::get_if<Object>(&data_);
    }

    Node::Object *Node::TryObject() noexcept
    {
        return std::get_if<Object>(&data_);
    }

    const Node::Array *Node::TryArray() const noexcept
    {
        return std::get_if<Array>(&data_);
    }

    Node::Array *Node::TryArray() noexcept
    {
        return std::get_if<Array>(&data_);
    }

    Result<std::vector<PathToken>> ParsePath(std::string_view path)
    {
        if (path.empty())
        {
            return Result<std::vector<PathToken>>{false, {}, "path must not be empty"};
        }

        std::vector<PathToken> out;
        std::string current;

        std::size_t i = 0;
        while (i < path.size())
        {
            const char ch = path[i];
            if (ch == '\\')
            {
                if (i + 1 >= path.size())
                {
                    return Result<std::vector<PathToken>>{false, {}, PathError(i, "trailing escape")};
                }
                current += path[i + 1];
                i += 2;
                continue;
            }

            if (ch == '.')
            {
                if (i + 1 >= path.size())
                {
                    return Result<std::vector<PathToken>>{false, {}, PathError(i, "trailing '.' is not allowed")};
                }

                if (current.empty())
                {
                    if (out.empty() || out.back().kind != PathTokenKind::Index)
                    {
                        return Result<std::vector<PathToken>>{false, {}, PathError(i, "empty path segment")};
                    }
                    ++i;
                    continue;
                }
                out.push_back(PathToken{PathTokenKind::Key, current, 0});
                current.clear();
                ++i;
                continue;
            }

            if (ch == '[')
            {
                if (!current.empty())
                {
                    out.push_back(PathToken{PathTokenKind::Key, current, 0});
                    current.clear();
                }

                ++i;
                if (i >= path.size())
                {
                    return Result<std::vector<PathToken>>{false, {}, PathError(i, "unterminated index")};
                }

                std::size_t index = 0;
                const std::size_t start = i;
                while (i < path.size() && path[i] >= '0' && path[i] <= '9')
                {
                    index = index * 10 + static_cast<std::size_t>(path[i] - '0');
                    ++i;
                }

                if (i == start)
                {
                    return Result<std::vector<PathToken>>{false, {}, PathError(i, "index must contain digits")};
                }

                if (i >= path.size() || path[i] != ']')
                {
                    return Result<std::vector<PathToken>>{false, {}, PathError(i, "missing closing ']' for index")};
                }

                out.push_back(PathToken{PathTokenKind::Index, "", index});
                ++i;

                if (i < path.size() && path[i] != '.' && path[i] != '[')
                {
                    return Result<std::vector<PathToken>>{false, {}, PathError(i, "expected '.' or '[' after index")};
                }
                continue;
            }

            current += ch;
            ++i;
        }

        if (!current.empty())
        {
            out.push_back(PathToken{PathTokenKind::Key, current, 0});
        }

        if (out.empty())
        {
            return Result<std::vector<PathToken>>{false, {}, "path did not produce tokens"};
        }

        return Result<std::vector<PathToken>>{true, std::move(out), ""};
    }

    Result<const Node *> GetNode(const Node &root, std::string_view path)
    {
        const auto parsed = ParsePath(path);
        if (!parsed.ok)
        {
            return Result<const Node *>{false, nullptr, parsed.error};
        }

        const Node *current = &root;
        for (const auto &token : parsed.value)
        {
            if (token.kind == PathTokenKind::Key)
            {
                const auto *obj = current->TryObject();
                if (obj == nullptr)
                {
                    return Result<const Node *>{false, nullptr, "type mismatch: expected object while traversing key '" + token.key + "'"};
                }

                const auto index = FindObjectIndex(*obj, token.key);
                if (!index.has_value())
                {
                    return Result<const Node *>{false, nullptr, "key not found: '" + token.key + "'"};
                }
                current = &(*obj)[*index].second;
                continue;
            }

            const auto *arr = current->TryArray();
            if (arr == nullptr)
            {
                return Result<const Node *>{false, nullptr, "type mismatch: expected array while traversing index"};
            }
            if (token.index >= arr->size())
            {
                return Result<const Node *>{false, nullptr, "array index out of range"};
            }
            current = &(*arr)[token.index];
        }

        return Result<const Node *>{true, current, ""};
    }

    Result<Node *> GetNodeMutable(Node &root, std::string_view path)
    {
        auto result = GetNode(static_cast<const Node &>(root), path);
        if (!result.ok)
        {
            return Result<Node *>{false, nullptr, result.error};
        }
        return Result<Node *>{true, const_cast<Node *>(result.value), ""};
    }

    Status SetNode(Node &root, std::string_view path, Node value)
    {
        const auto parsed = ParsePath(path);
        if (!parsed.ok)
        {
            return FailStatus(parsed.error);
        }

        Node *current = &root;
        for (std::size_t i = 0; i < parsed.value.size(); ++i)
        {
            const bool is_last = (i + 1 == parsed.value.size());
            const auto &token = parsed.value[i];

            if (token.kind == PathTokenKind::Key)
            {
                if (current->IsNull())
                {
                    *current = Node::MakeObject();
                }
                auto *obj = current->TryObject();
                if (obj == nullptr)
                {
                    return FailStatus("type mismatch: expected object while setting key '" + token.key + "'");
                }

                auto index = FindObjectIndex(*obj, token.key);
                if (!index.has_value())
                {
                    obj->push_back({token.key, Node{}});
                    index = obj->size() - 1;
                }

                if (is_last)
                {
                    (*obj)[*index].second = std::move(value);
                    return OkStatus();
                }

                current = &(*obj)[*index].second;
                continue;
            }

            if (current->IsNull())
            {
                *current = Node::MakeArray();
            }
            auto *arr = current->TryArray();
            if (arr == nullptr)
            {
                return FailStatus("type mismatch: expected array while setting index");
            }

            if (token.index >= arr->size())
            {
                arr->resize(token.index + 1);
            }

            if (is_last)
            {
                (*arr)[token.index] = std::move(value);
                return OkStatus();
            }

            current = &(*arr)[token.index];
        }

        return FailStatus("set operation failed");
    }

    Status RemoveNode(Node &root, std::string_view path)
    {
        const auto parsed = ParsePath(path);
        if (!parsed.ok)
        {
            return FailStatus(parsed.error);
        }

        if (parsed.value.empty())
        {
            return FailStatus("path must not be empty");
        }

        Node *current = &root;
        for (std::size_t i = 0; i + 1 < parsed.value.size(); ++i)
        {
            const auto &token = parsed.value[i];
            if (token.kind == PathTokenKind::Key)
            {
                auto *obj = current->TryObject();
                if (obj == nullptr)
                {
                    return FailStatus("type mismatch: expected object while removing path");
                }
                const auto index = FindObjectIndex(*obj, token.key);
                if (!index.has_value())
                {
                    return FailStatus("key not found: '" + token.key + "'");
                }
                current = &(*obj)[*index].second;
                continue;
            }

            auto *arr = current->TryArray();
            if (arr == nullptr)
            {
                return FailStatus("type mismatch: expected array while removing path");
            }
            if (token.index >= arr->size())
            {
                return FailStatus("array index out of range");
            }
            current = &(*arr)[token.index];
        }

        const auto &last = parsed.value.back();
        if (last.kind == PathTokenKind::Key)
        {
            auto *obj = current->TryObject();
            if (obj == nullptr)
            {
                return FailStatus("type mismatch: expected object for remove key");
            }
            const auto index = FindObjectIndex(*obj, last.key);
            if (!index.has_value())
            {
                return FailStatus("key not found: '" + last.key + "'");
            }
            obj->erase(obj->begin() + static_cast<std::ptrdiff_t>(*index));
            return OkStatus();
        }

        auto *arr = current->TryArray();
        if (arr == nullptr)
        {
            return FailStatus("type mismatch: expected array for remove index");
        }
        if (last.index >= arr->size())
        {
            return FailStatus("array index out of range");
        }
        arr->erase(arr->begin() + static_cast<std::ptrdiff_t>(last.index));
        return OkStatus();
    }

    RuntimeOverrides::RuntimeOverrides() = default;

    Status RuntimeOverrides::Set(std::string_view path, Node value)
    {
        const auto parsed = ParsePath(path);
        if (!parsed.ok)
        {
            return FailStatus(parsed.error);
        }

        Op op;
        op.sequence = next_sequence_++;
        op.kind = OpKind::Set;
        op.path = std::string(path);
        op.value = std::move(value);
        ops_.push_back(std::move(op));
        return OkStatus();
    }

    Status RuntimeOverrides::Remove(std::string_view path)
    {
        const auto parsed = ParsePath(path);
        if (!parsed.ok)
        {
            return FailStatus(parsed.error);
        }

        Op op;
        op.sequence = next_sequence_++;
        op.kind = OpKind::Remove;
        op.path = std::string(path);
        ops_.push_back(std::move(op));
        return OkStatus();
    }

    Status RuntimeOverrides::Replace(Node value)
    {
        Op op;
        op.sequence = next_sequence_++;
        op.kind = OpKind::Replace;
        op.value = std::move(value);
        ops_.push_back(std::move(op));
        return OkStatus();
    }

    void RuntimeOverrides::Clear() noexcept
    {
        ops_.clear();
        next_sequence_ = 1;
    }

    bool RuntimeOverrides::Empty() const noexcept
    {
        return ops_.empty();
    }

    Result<Node> RuntimeOverrides::Materialize() const
    {
        Node out = Node::MakeObject();

        for (const auto &op : ops_)
        {
            if (op.kind == OpKind::Replace)
            {
                out = op.value;
                continue;
            }

            if (op.kind == OpKind::Set)
            {
                auto st = SetNode(out, op.path, op.value);
                if (!st.ok)
                {
                    return Result<Node>{false, Node{}, "runtime set failed at path '" + op.path + "': " + st.error};
                }
                continue;
            }

            auto st = RemoveNode(out, op.path);
            if (!st.ok && !IsMissingPathError(st.error))
            {
                return Result<Node>{false, Node{}, "runtime remove failed at path '" + op.path + "': " + st.error};
            }
        }

        return Result<Node>{true, std::move(out), ""};
    }

    bool Exists(const Node &root, std::string_view path)
    {
        return GetNode(root, path).ok;
    }

    bool IsEmptyValue(const Node &node) noexcept
    {
        return node.IsEmpty();
    }

    Status Merge(Node &base, const Node &overlay, bool append_arrays)
    {
        return MergeInternal(base, overlay, append_arrays);
    }

    Result<Node> BuildEnvLayerFromPairs(const std::vector<std::pair<std::string, std::string>> &variables,
                                        std::string_view prefix,
                                        const TypePolicyOptions &policy)
    {
        if (prefix.empty())
        {
            return Result<Node>{false, Node{}, "env prefix must not be empty"};
        }

        Node root = Node::MakeObject();
        auto sorted = variables;
        std::sort(sorted.begin(), sorted.end(), [](const auto &lhs, const auto &rhs)
                  {
                      if (lhs.first == rhs.first)
                      {
                          return lhs.second < rhs.second;
                      }
                      return lhs.first < rhs.first; });

        for (const auto &entry : sorted)
        {
            std::string path_error;
            const auto mapped_path = ParseEnvPath(entry.first, prefix, path_error);
            if (!mapped_path.has_value())
            {
                if (path_error.empty())
                {
                    continue;
                }
                return Result<Node>{false, Node{}, path_error};
            }

            const bool strict_string = ResolveStrictStringPolicy(policy, SourceLayer::Env, *mapped_path);
            auto st = SetNode(root, *mapped_path, ParseScalar(entry.second, strict_string));
            if (!st.ok)
            {
                return Result<Node>{false, Node{}, "failed to map env key '" + entry.first + "': " + st.error};
            }
        }

        return Result<Node>{true, std::move(root), ""};
    }

    Result<Node> BuildEnvLayerFromEnvironment(std::string_view prefix,
                                              const TypePolicyOptions &policy)
    {
        const auto variables = CollectEnvironmentPairs();
        return BuildEnvLayerFromPairs(variables, prefix, policy);
    }

    Result<Node> ComposeLayers(const Node &base,
                               const std::optional<Node> &env_layer,
                               const std::optional<Node> &local_layer,
                               const RuntimeOverrides *runtime,
                               const ComposeOptions &options,
                               std::vector<SourceAttribution> *source_trace,
                               const std::optional<Node> &remote_layer)
    {
        Node out = base;

        if (env_layer.has_value())
        {
            auto st = Merge(out, *env_layer, options.append_arrays);
            if (!st.ok)
            {
                return Result<Node>{false, Node{}, "compose env layer failed: " + st.error};
            }
        }

        if (remote_layer.has_value())
        {
            auto st = Merge(out, *remote_layer, options.append_arrays);
            if (!st.ok)
            {
                return Result<Node>{false, Node{}, "compose remote layer failed: " + st.error};
            }
        }

        if (local_layer.has_value())
        {
            auto st = Merge(out, *local_layer, options.append_arrays);
            if (!st.ok)
            {
                return Result<Node>{false, Node{}, "compose local layer failed: " + st.error};
            }
        }

        Node runtime_layer;
        bool has_runtime_layer = false;
        if (runtime != nullptr && !runtime->Empty())
        {
            const auto resolved = runtime->Materialize();
            if (!resolved.ok)
            {
                return Result<Node>{false, Node{}, "compose runtime layer failed: " + resolved.error};
            }

            runtime_layer = resolved.value;
            has_runtime_layer = true;

            auto st = Merge(out, runtime_layer, options.append_arrays);
            if (!st.ok)
            {
                return Result<Node>{false, Node{}, "compose runtime merge failed: " + st.error};
            }
        }

        if (source_trace != nullptr)
        {
            std::unordered_map<std::string, SourceLayer> trace_index;

            const auto collect_layer = [&](const Node &layer_node, SourceLayer layer)
            {
                std::function<void(const Node &, const std::string &)> visit;
                visit = [&](const Node &node, const std::string &path)
                {
                    if (node.IsObject())
                    {
                        const auto *obj = node.TryObject();
                        for (const auto &kv : *obj)
                        {
                            const std::string next = path.empty() ? kv.first : (path + "." + kv.first);
                            visit(kv.second, next);
                        }
                        return;
                    }

                    if (node.IsArray())
                    {
                        const auto *arr = node.TryArray();
                        for (std::size_t i = 0; i < arr->size(); ++i)
                        {
                            const std::string next = path + "[" + std::to_string(i) + "]";
                            visit((*arr)[i], next);
                        }
                        return;
                    }

                    const std::string normalized = path.empty() ? "value" : path;
                    trace_index[normalized] = layer;
                };

                visit(layer_node, "");
            };

            collect_layer(base, SourceLayer::Base);
            if (env_layer.has_value())
            {
                collect_layer(*env_layer, SourceLayer::Env);
            }
            if (remote_layer.has_value())
            {
                collect_layer(*remote_layer, SourceLayer::Remote);
            }
            if (local_layer.has_value())
            {
                collect_layer(*local_layer, SourceLayer::Local);
            }
            if (has_runtime_layer)
            {
                collect_layer(runtime_layer, SourceLayer::Runtime);
            }

            source_trace->clear();
            source_trace->reserve(trace_index.size());
            for (const auto &entry : trace_index)
            {
                source_trace->push_back(SourceAttribution{entry.first, entry.second});
            }

            std::sort(source_trace->begin(), source_trace->end(), [](const SourceAttribution &lhs, const SourceAttribution &rhs)
                      { return lhs.path < rhs.path; });
        }

        return Result<Node>{true, std::move(out), ""};
    }

    PollReloader::PollReloader(std::string base_file_path,
                               std::string local_file_path,
                               const RuntimeOverrides *runtime)
        : base_file_path_(std::move(base_file_path)),
          local_file_path_(std::move(local_file_path)),
          runtime_(runtime)
    {
    }

    void PollReloader::SetRuntimeOverrides(const RuntimeOverrides *runtime) noexcept
    {
        runtime_ = runtime;
    }

    void PollReloader::SetValidationRules(std::vector<ValidationRule> rules)
    {
        rules_ = std::move(rules);
    }

    void PollReloader::SetCallback(std::function<void(const ReloadEvent &event)> callback)
    {
        callback_ = std::move(callback);
    }

    void PollReloader::SetOptions(ReloadOptions options)
    {
        options_ = std::move(options);
    }

    const Node *PollReloader::Current() const noexcept
    {
        return has_current_ ? &current_ : nullptr;
    }

    const std::vector<SourceAttribution> *PollReloader::CurrentSourceTrace() const noexcept
    {
        return has_current_ ? &current_trace_ : nullptr;
    }

    Result<Node> PollReloader::SnapshotCurrent() const
    {
        if (!has_current_)
        {
            return Result<Node>{false, Node{}, "no active config to snapshot"};
        }
        return Result<Node>{true, current_, ""};
    }

    Status PollReloader::ExportSnapshotToFile(std::string_view file_path,
                                              ConfigFormat preferred,
                                              int indent) const
    {
        if (!has_current_)
        {
            return FailStatus("no active config to snapshot");
        }
        return SaveToFile(current_, file_path, preferred, indent);
    }

    Result<Node> PollReloader::ImportSnapshotFromFile(std::string_view file_path,
                                                      ConfigFormat preferred) const
    {
        return LoadFromFile(file_path, preferred);
    }

    Status PollReloader::RestoreSnapshot(const Node &snapshot,
                                         const std::vector<SourceAttribution> *source_trace)
    {
        current_ = snapshot;
        has_current_ = true;
        pending_change_ = false;
        pending_since_ms_ = 0;
        last_remote_poll_ms_ = 0;

        if (source_trace != nullptr)
        {
            current_trace_ = *source_trace;
        }
        else
        {
            current_trace_.clear();
        }

        SnapshotAuditEntry entry;
        entry.sequence = audit_sequence_++;
        entry.timestamp_ms = SteadyNowMs();
        entry.rolled_back = false;
        entry.action = "restore_snapshot";
        entry.message = "snapshot restored";
        entry.source_trace = current_trace_;
        audit_trail_.push_back(std::move(entry));

        return OkStatus();
    }

    Status PollReloader::RestoreSnapshotFromFile(std::string_view file_path,
                                                 ConfigFormat preferred,
                                                 const std::vector<SourceAttribution> *source_trace)
    {
        const auto loaded = ImportSnapshotFromFile(file_path, preferred);
        if (!loaded.ok)
        {
            return FailStatus("failed to import snapshot: " + loaded.error);
        }
        return RestoreSnapshot(loaded.value, source_trace);
    }

    const std::vector<SnapshotAuditEntry> &PollReloader::AuditTrail() const noexcept
    {
        return audit_trail_;
    }

    void PollReloader::ClearAuditTrail() noexcept
    {
        audit_trail_.clear();
    }

    Result<ReloadEvent> PollReloader::ReloadNow()
    {
        ReloadEvent event;
        event.attempted = true;
        std::string remote_warning;

        auto notify = [&]()
        {
            if (callback_)
            {
                callback_(event);
            }
        };

        if (options_.include_snapshots && has_current_)
        {
            event.old_snapshot = current_;
        }

        auto rollback_or_fail = [&](std::string message) -> Result<ReloadEvent>
        {
            event.message = std::move(message);
            if (has_current_)
            {
                event.rolled_back = true;
                event.changed = false;
                event.source_trace = current_trace_;

                SnapshotAuditEntry entry;
                entry.sequence = audit_sequence_++;
                entry.timestamp_ms = SteadyNowMs();
                entry.rolled_back = true;
                entry.action = "reload_rollback";
                entry.message = event.message;
                entry.source_trace = event.source_trace;
                audit_trail_.push_back(std::move(entry));

                notify();
                return Result<ReloadEvent>{true, event, ""};
            }

            return Result<ReloadEvent>{false, ReloadEvent{}, event.message};
        };

        Node base = Node::MakeObject();
        if (!base_file_path_.empty())
        {
            const auto fp = CaptureFileFingerprint(base_file_path_, options_.allow_missing_base);
            if (!fp.ok)
            {
                return rollback_or_fail(fp.error);
            }
            if (fp.value.exists)
            {
                const auto loaded = LoadFromFile(base_file_path_);
                if (!loaded.ok)
                {
                    return rollback_or_fail("failed to parse base file: " + loaded.error);
                }
                base = loaded.value;
            }
        }

        std::optional<Node> local_layer;
        if (!local_file_path_.empty())
        {
            const auto fp = CaptureFileFingerprint(local_file_path_, options_.allow_missing_local);
            if (!fp.ok)
            {
                return rollback_or_fail(fp.error);
            }
            if (fp.value.exists)
            {
                const auto loaded = LoadFromFile(local_file_path_);
                if (!loaded.ok)
                {
                    return rollback_or_fail("failed to parse local file: " + loaded.error);
                }
                local_layer = loaded.value;
            }
        }

        std::optional<Node> remote_layer;
        const std::string remote_url = TrimCopy(options_.remote_url);
        if (!remote_url.empty())
        {
            const auto loaded = LoadFromRemote(remote_url, options_.remote_format, options_.remote_headers);
            if (!loaded.ok)
            {
                if (!options_.allow_remote_failure || has_current_)
                {
                    return rollback_or_fail("failed to fetch remote config: " + loaded.error);
                }
                remote_warning = "remote fetch skipped: " + loaded.error;
            }
            else
            {
                remote_layer = loaded.value;
            }
        }

        const auto env_layer = BuildEnvLayerFromEnvironment(options_.env_prefix, options_.type_policy);
        if (!env_layer.ok)
        {
            return rollback_or_fail("failed to build env layer: " + env_layer.error);
        }

        std::vector<SourceAttribution> trace;
        const auto composed = ComposeLayers(base,
                                            env_layer.value,
                                            local_layer,
                                            runtime_,
                                            options_.compose_options,
                                            &trace,
                                            remote_layer);
        if (!composed.ok)
        {
            return rollback_or_fail("compose failed: " + composed.error);
        }

        if (!rules_.empty())
        {
            const auto validation = Validate(composed.value, rules_);
            if (!validation.ok)
            {
                std::ostringstream oss;
                oss << "validation failed with " << validation.value.size() << " issue(s)";
                return rollback_or_fail(oss.str());
            }
        }

        event.source_trace = trace;
        event.diff_paths = BuildDiffEntries(has_current_ ? &current_ : nullptr, composed.value, trace);
        event.changed = !has_current_ || !event.diff_paths.empty();
        event.rolled_back = false;
        event.message = event.changed ? "reload applied" : "reload no-op";
        if (!remote_warning.empty())
        {
            event.message += "; " + remote_warning;
        }

        if (options_.include_snapshots)
        {
            event.new_snapshot = composed.value;
        }

        SnapshotAuditEntry entry;
        entry.sequence = audit_sequence_++;
        entry.timestamp_ms = SteadyNowMs();
        entry.rolled_back = false;
        entry.action = event.changed ? "reload_applied" : "reload_noop";
        entry.message = event.message;
        entry.diff_paths = event.diff_paths;
        entry.source_trace = event.source_trace;
        audit_trail_.push_back(std::move(entry));

        current_ = composed.value;
        current_trace_ = std::move(trace);
        has_current_ = true;

        auto sync_state = [&](TrackedFileState &state, std::string_view path, bool allow_missing)
        {
            const auto fp = CaptureFileFingerprint(path, allow_missing);
            if (!fp.ok)
            {
                return;
            }
            state.initialized = true;
            state.exists = fp.value.exists;
            state.mtime = fp.value.mtime;
            state.content_hash = fp.value.content_hash;
        };

        sync_state(base_state_, base_file_path_, options_.allow_missing_base);
        sync_state(local_state_, local_file_path_, options_.allow_missing_local);

        notify();
        return Result<ReloadEvent>{true, std::move(event), ""};
    }

    Result<ReloadEvent> PollReloader::Tick(std::uint64_t now_ms)
    {
        if (now_ms == 0)
        {
            now_ms = SteadyNowMs();
        }

        const bool remote_poll_enabled = !TrimCopy(options_.remote_url).empty() &&
                                         options_.remote_poll_interval_ms > 0;
        bool remote_due = false;
        if (remote_poll_enabled)
        {
            if (last_remote_poll_ms_ == 0)
            {
                remote_due = true;
            }
            else if (now_ms >= last_remote_poll_ms_)
            {
                remote_due = (now_ms - last_remote_poll_ms_) >= options_.remote_poll_interval_ms;
            }
        }

        auto read_state = [&](std::string_view path, bool allow_missing, TrackedFileState &tracked) -> Result<TrackedFileState>
        {
            const auto fp = CaptureFileFingerprint(path, allow_missing);
            if (!fp.ok)
            {
                return Result<TrackedFileState>{false, {}, fp.error};
            }

            TrackedFileState observed;
            observed.initialized = true;
            observed.exists = fp.value.exists;
            observed.mtime = fp.value.mtime;
            observed.content_hash = fp.value.content_hash;
            return Result<TrackedFileState>{true, observed, ""};
        };

        const auto base_observed = read_state(base_file_path_, options_.allow_missing_base, base_state_);
        if (!base_observed.ok)
        {
            return Result<ReloadEvent>{false, ReloadEvent{}, base_observed.error};
        }
        const auto local_observed = read_state(local_file_path_, options_.allow_missing_local, local_state_);
        if (!local_observed.ok)
        {
            return Result<ReloadEvent>{false, ReloadEvent{}, local_observed.error};
        }

        const auto state_changed = [](const TrackedFileState &tracked, const TrackedFileState &observed)
        {
            if (!tracked.initialized)
            {
                return true;
            }
            if (tracked.exists != observed.exists)
            {
                return true;
            }
            if (!observed.exists)
            {
                return false;
            }
            // mtime + hash double-check: mtime drift alone is ignored when hash is unchanged.
            return tracked.content_hash != observed.content_hash;
        };

        const bool changed = state_changed(base_state_, base_observed.value) ||
                             state_changed(local_state_, local_observed.value);

        if (!changed && !remote_due)
        {
            pending_change_ = false;
            ReloadEvent idle;
            idle.attempted = false;
            idle.changed = false;
            idle.message = remote_poll_enabled ? "no file or remote changes detected" : "no file changes detected";
            return Result<ReloadEvent>{true, std::move(idle), ""};
        }

        if (!has_current_)
        {
            base_state_ = base_observed.value;
            local_state_ = local_observed.value;
            pending_change_ = false;
            if (remote_poll_enabled)
            {
                last_remote_poll_ms_ = now_ms;
            }
            return ReloadNow();
        }

        if (options_.debounce_ms > 0)
        {
            if (!pending_change_)
            {
                pending_change_ = true;
                pending_since_ms_ = now_ms;
                ReloadEvent waiting;
                waiting.attempted = false;
                waiting.changed = false;
                waiting.message = "debounce window active";
                return Result<ReloadEvent>{true, std::move(waiting), ""};
            }

            const std::uint64_t elapsed = (now_ms >= pending_since_ms_) ? (now_ms - pending_since_ms_) : 0;
            if (elapsed < options_.debounce_ms)
            {
                ReloadEvent waiting;
                waiting.attempted = false;
                waiting.changed = false;
                waiting.message = "debounce window active";
                return Result<ReloadEvent>{true, std::move(waiting), ""};
            }
        }

        pending_change_ = false;
        base_state_ = base_observed.value;
        local_state_ = local_observed.value;
        if (remote_poll_enabled)
        {
            last_remote_poll_ms_ = now_ms;
        }
        return ReloadNow();
    }

    Result<std::vector<ValidationIssue>> Validate(const Node &root, const std::vector<ValidationRule> &rules)
    {
        std::vector<ValidationIssue> issues;
        for (const auto &rule : rules)
        {
            if (!rule.evaluator)
            {
                continue;
            }

            const auto issue = rule.evaluator(root);
            if (!issue.has_value())
            {
                continue;
            }

            ValidationIssue captured = *issue;
            if (!rule.name.empty())
            {
                captured.message = "[" + rule.name + "] " + captured.message;
            }
            issues.push_back(std::move(captured));

            if (rule.fail_fast)
            {
                break;
            }
        }

        const bool passed = issues.empty();
        Result<std::vector<ValidationIssue>> out;
        out.ok = passed;
        out.value = std::move(issues);
        out.error = passed ? "" : "validation failed";
        return out;
    }

    ValidationRule RequirePathRule(std::string path, bool fail_fast)
    {
        ValidationRule rule;
        rule.name = "require";
        rule.fail_fast = fail_fast;
        rule.evaluator = [path = std::move(path)](const Node &root) -> std::optional<ValidationIssue>
        {
            if (Exists(root, path))
            {
                return std::nullopt;
            }

            ValidationIssue issue;
            issue.path = path;
            issue.message = "required path is missing";
            return issue;
        };
        return rule;
    }

    ValidationRule ExpectKindRule(std::string path, NodeKind expected_kind, bool fail_fast)
    {
        ValidationRule rule;
        rule.name = "expect-kind";
        rule.fail_fast = fail_fast;
        rule.evaluator = [path = std::move(path), expected_kind](const Node &root) -> std::optional<ValidationIssue>
        {
            const auto node = GetNode(root, path);
            if (!node.ok || node.value == nullptr)
            {
                ValidationIssue issue;
                issue.path = path;
                issue.message = "path not found while checking kind";
                return issue;
            }

            if (node.value->Kind() == expected_kind)
            {
                return std::nullopt;
            }

            ValidationIssue issue;
            issue.path = path;
            issue.message = std::string("expected kind '") + ToString(expected_kind) +
                            "' but got '" + ToString(node.value->Kind()) + "'";
            return issue;
        };
        return rule;
    }

    ValidationRule NumericRangeRule(std::string path,
                                    double min_value,
                                    double max_value,
                                    bool fail_fast)
    {
        ValidationRule rule;
        rule.name = "numeric-range";
        rule.fail_fast = fail_fast;
        rule.evaluator = [path = std::move(path), min_value, max_value](const Node &root) -> std::optional<ValidationIssue>
        {
            const auto node = GetNode(root, path);
            if (!node.ok || node.value == nullptr)
            {
                ValidationIssue issue;
                issue.path = path;
                issue.message = "path not found while checking numeric range";
                return issue;
            }

            const auto kind = node.value->Kind();
            if (kind != NodeKind::Integer && kind != NodeKind::Double)
            {
                ValidationIssue issue;
                issue.path = path;
                issue.message = "range check requires integer or double";
                return issue;
            }

            const double value = node.value->AsDouble();
            if (value < min_value || value > max_value)
            {
                std::ostringstream oss;
                oss << "numeric value " << value << " is out of range ["
                    << min_value << "," << max_value << "]";

                ValidationIssue issue;
                issue.path = path;
                issue.message = oss.str();
                return issue;
            }

            return std::nullopt;
        };
        return rule;
    }

    ValidationRule ChoiceRule(std::string path,
                              std::vector<std::string> choices,
                              bool case_insensitive,
                              bool fail_fast)
    {
        ValidationRule rule;
        rule.name = "choice";
        rule.fail_fast = fail_fast;
        rule.evaluator = [path = std::move(path), choices = std::move(choices), case_insensitive](const Node &root) -> std::optional<ValidationIssue>
        {
            if (choices.empty())
            {
                return ValidationIssue{path, "choice rule has empty candidate set"};
            }

            const auto node = GetNode(root, path);
            if (!node.ok || node.value == nullptr)
            {
                return ValidationIssue{path, "path not found while checking choice"};
            }

            std::string actual;
            switch (node.value->Kind())
            {
            case NodeKind::Null:
                actual = "null";
                break;
            case NodeKind::Bool:
                actual = node.value->AsBool() ? "true" : "false";
                break;
            case NodeKind::Integer:
                actual = std::to_string(node.value->AsInt());
                break;
            case NodeKind::Double:
            {
                std::ostringstream oss;
                oss << node.value->AsDouble();
                actual = oss.str();
                break;
            }
            case NodeKind::String:
                actual = node.value->AsString();
                break;
            default:
                return ValidationIssue{path, "choice rule requires scalar node"};
            }

            const std::string normalized_actual = case_insensitive ? ToLowerAscii(actual) : actual;
            bool matched = false;
            for (const auto &candidate : choices)
            {
                const std::string normalized_candidate = case_insensitive ? ToLowerAscii(candidate) : candidate;
                if (normalized_actual == normalized_candidate)
                {
                    matched = true;
                    break;
                }
            }

            if (matched)
            {
                return std::nullopt;
            }

            std::ostringstream oss;
            oss << "value '" << actual << "' is not in choice set {";
            for (std::size_t i = 0; i < choices.size(); ++i)
            {
                if (i > 0)
                {
                    oss << ",";
                }
                oss << choices[i];
            }
            oss << "}";
            return ValidationIssue{path, oss.str()};
        };
        return rule;
    }

    ValidationRule MutexRule(std::vector<std::string> paths,
                             bool fail_fast)
    {
        ValidationRule rule;
        rule.name = "mutex";
        rule.fail_fast = fail_fast;
        rule.evaluator = [paths = std::move(paths)](const Node &root) -> std::optional<ValidationIssue>
        {
            std::vector<std::string> existing;
            for (const auto &path : paths)
            {
                if (TrimCopy(path).empty())
                {
                    continue;
                }
                if (Exists(root, path))
                {
                    existing.push_back(path);
                }
            }

            if (existing.size() <= 1)
            {
                return std::nullopt;
            }

            std::ostringstream oss;
            oss << "mutually exclusive paths are set: ";
            for (std::size_t i = 0; i < existing.size(); ++i)
            {
                if (i > 0)
                {
                    oss << ",";
                }
                oss << existing[i];
            }
            return ValidationIssue{existing.front(), oss.str()};
        };
        return rule;
    }

    ValidationRule DependencyRule(std::string path,
                                  std::string depends_on,
                                  bool fail_fast)
    {
        ValidationRule rule;
        rule.name = "dependency";
        rule.fail_fast = fail_fast;
        rule.evaluator = [path = std::move(path), depends_on = std::move(depends_on)](const Node &root) -> std::optional<ValidationIssue>
        {
            if (!Exists(root, path))
            {
                return std::nullopt;
            }

            if (Exists(root, depends_on))
            {
                return std::nullopt;
            }

            return ValidationIssue{path, "path requires dependency '" + depends_on + "'"};
        };
        return rule;
    }

    ValidationRule StringLengthRule(std::string path,
                                    std::size_t min_length,
                                    std::size_t max_length,
                                    bool fail_fast)
    {
        ValidationRule rule;
        rule.name = "string-length";
        rule.fail_fast = fail_fast;
        rule.evaluator = [path = std::move(path), min_length, max_length](const Node &root) -> std::optional<ValidationIssue>
        {
            if (min_length > max_length)
            {
                return ValidationIssue{path, "invalid rule: min_length > max_length"};
            }

            const auto node = GetNode(root, path);
            if (!node.ok || node.value == nullptr)
            {
                return ValidationIssue{path, "path not found while checking string length"};
            }

            if (node.value->Kind() != NodeKind::String)
            {
                return ValidationIssue{path, "string length rule requires string node"};
            }

            const std::size_t len = node.value->AsString().size();
            if (len < min_length || len > max_length)
            {
                std::ostringstream oss;
                oss << "string length " << len << " is out of range [" << min_length << "," << max_length << "]";
                return ValidationIssue{path, oss.str()};
            }

            return std::nullopt;
        };
        return rule;
    }

    ConfigFormat DetectFormatFromPath(std::string_view file_path)
    {
        const std::string lower = ToLowerAscii(file_path);
        if (lower.size() >= 5 && lower.substr(lower.size() - 5) == ".json")
        {
            return ConfigFormat::Json;
        }
        if ((lower.size() >= 4 && lower.substr(lower.size() - 4) == ".ini") ||
            (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".cfg"))
        {
            return ConfigFormat::Ini;
        }
        if ((lower.size() >= 5 && lower.substr(lower.size() - 5) == ".yaml") ||
            (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".yml"))
        {
            return ConfigFormat::Yaml;
        }
        if (lower.size() >= 5 && lower.substr(lower.size() - 5) == ".toml")
        {
            return ConfigFormat::Toml;
        }
        return ConfigFormat::Unknown;
    }

    Status RegisterParserAdapter(ParserAdapter adapter, bool set_active)
    {
        const std::string key = NormalizeAdapterKey(adapter.name);
        if (key.empty())
        {
            return FailStatus("adapter name must not be empty");
        }
        if (!adapter.parse)
        {
            return FailStatus("adapter parse callback must not be empty");
        }
        if (!adapter.dump)
        {
            return FailStatus("adapter dump callback must not be empty");
        }

        std::lock_guard<std::mutex> lock(AdapterRegistryMutex());
        AdapterRegistry()[key] = std::move(adapter);
        if (set_active)
        {
            ActiveAdapterKey() = key;
        }
        return OkStatus();
    }

    Status UnregisterParserAdapter(std::string_view name)
    {
        const std::string key = NormalizeAdapterKey(name);
        if (key.empty())
        {
            return FailStatus("adapter name must not be empty");
        }

        std::lock_guard<std::mutex> lock(AdapterRegistryMutex());
        const auto erased = AdapterRegistry().erase(key);
        if (erased == 0)
        {
            return FailStatus("adapter not found: " + std::string(name));
        }

        if (ActiveAdapterKey() == key)
        {
            ActiveAdapterKey().clear();
        }
        return OkStatus();
    }

    Status SetActiveParserAdapter(std::string_view name)
    {
        const std::string key = NormalizeAdapterKey(name);
        std::lock_guard<std::mutex> lock(AdapterRegistryMutex());

        if (key.empty())
        {
            ActiveAdapterKey().clear();
            return OkStatus();
        }

        if (AdapterRegistry().find(key) == AdapterRegistry().end())
        {
            return FailStatus("adapter not found: " + std::string(name));
        }

        ActiveAdapterKey() = key;
        return OkStatus();
    }

    std::string GetActiveParserAdapter()
    {
        std::lock_guard<std::mutex> lock(AdapterRegistryMutex());
        const std::string key = ActiveAdapterKey();
        if (key.empty())
        {
            return "";
        }

        const auto it = AdapterRegistry().find(key);
        if (it == AdapterRegistry().end())
        {
            return "";
        }
        return it->second.name;
    }

    std::vector<std::string> ListParserAdapters()
    {
        std::lock_guard<std::mutex> lock(AdapterRegistryMutex());
        std::vector<std::string> out;
        out.reserve(AdapterRegistry().size());
        for (const auto &entry : AdapterRegistry())
        {
            out.push_back(entry.second.name);
        }
        std::sort(out.begin(), out.end());
        return out;
    }

    bool HasParserAdapter(std::string_view name)
    {
        const std::string key = NormalizeAdapterKey(name);
        if (key.empty())
        {
            return false;
        }

        std::lock_guard<std::mutex> lock(AdapterRegistryMutex());
        return AdapterRegistry().find(key) != AdapterRegistry().end();
    }

    void ClearParserAdapters()
    {
        std::lock_guard<std::mutex> lock(AdapterRegistryMutex());
        AdapterRegistry().clear();
        ActiveAdapterKey().clear();
    }

    Status SetRemoteFetcher(RemoteFetcher fetcher)
    {
        std::lock_guard<std::mutex> lock(RemoteFetcherMutex());
        RemoteFetcherState() = std::move(fetcher);
        return OkStatus();
    }

    bool HasRemoteFetcher()
    {
        std::lock_guard<std::mutex> lock(RemoteFetcherMutex());
        return static_cast<bool>(RemoteFetcherState());
    }

    const char *ToString(ConfigFormat format) noexcept
    {
        switch (format)
        {
        case ConfigFormat::Json:
            return "json";
        case ConfigFormat::Ini:
            return "ini/cfg";
        case ConfigFormat::Yaml:
            return "yaml";
        case ConfigFormat::Toml:
            return "toml";
        default:
            return "unknown";
        }
    }

    const char *ToString(NodeKind kind) noexcept
    {
        switch (kind)
        {
        case NodeKind::Null:
            return "null";
        case NodeKind::Bool:
            return "bool";
        case NodeKind::Integer:
            return "integer";
        case NodeKind::Double:
            return "double";
        case NodeKind::String:
            return "string";
        case NodeKind::Object:
            return "object";
        case NodeKind::Array:
            return "array";
        default:
            return "unknown";
        }
    }

    const char *ToString(SourceLayer layer) noexcept
    {
        switch (layer)
        {
        case SourceLayer::Base:
            return "base";
        case SourceLayer::Env:
            return "env";
        case SourceLayer::Remote:
            return "remote";
        case SourceLayer::Local:
            return "local";
        case SourceLayer::Runtime:
            return "runtime";
        default:
            return "unknown";
        }
    }

    const char *ToString(DiffKind kind) noexcept
    {
        switch (kind)
        {
        case DiffKind::Added:
            return "added";
        case DiffKind::Removed:
            return "removed";
        case DiffKind::Changed:
            return "changed";
        default:
            return "unknown";
        }
    }

    std::optional<NodeKind> ParseNodeKind(std::string_view text)
    {
        const std::string lower = ToLowerAscii(TrimCopy(text));
        if (lower == "null")
        {
            return NodeKind::Null;
        }
        if (lower == "bool" || lower == "boolean")
        {
            return NodeKind::Bool;
        }
        if (lower == "int" || lower == "integer")
        {
            return NodeKind::Integer;
        }
        if (lower == "double" || lower == "number" || lower == "float")
        {
            return NodeKind::Double;
        }
        if (lower == "string" || lower == "str")
        {
            return NodeKind::String;
        }
        if (lower == "object" || lower == "map")
        {
            return NodeKind::Object;
        }
        if (lower == "array" || lower == "list")
        {
            return NodeKind::Array;
        }

        return std::nullopt;
    }

    Result<Node> ParseJson(std::string_view text)
    {
        JsonParser parser(text);
        return parser.Parse();
    }

    std::string ToJson(const Node &node, int indent)
    {
        std::ostringstream oss;
        const int safe_indent = indent < 0 ? 0 : indent;
        AppendJson(node, oss, safe_indent, 0);
        return oss.str();
    }

    namespace
    {
        Result<Node> ParseIniText(std::string_view text);
        Result<Node> ParseYamlText(std::string_view text);
        Result<Node> ParseTomlText(std::string_view text);
    }

    Result<Node> LoadFromRemote(std::string_view url,
                                ConfigFormat preferred,
                                HeaderList headers)
    {
        const auto fetcher = AcquireRemoteFetcherCopy();
        if (!fetcher)
        {
            return Result<Node>{false, Node{}, "remote fetcher is not configured"};
        }

        if (TrimCopy(url).empty())
        {
            return Result<Node>{false, Node{}, "remote url must not be empty"};
        }

        RemoteFetchRequest request;
        request.url = std::string(url);
        request.headers = std::move(headers);

        const auto fetched = fetcher(request);
        if (!fetched.ok)
        {
            return Result<Node>{false, Node{}, fetched.error.empty() ? "remote fetch failed" : fetched.error};
        }

        const ConfigFormat format = (preferred == ConfigFormat::Unknown)
                                        ? DetectFormatFromPath(StripUrlQueryAndFragment(url))
                                        : preferred;
        if (format == ConfigFormat::Unknown)
        {
            return Result<Node>{false, Node{}, "cannot detect config format from remote url"};
        }

        const std::string &text = fetched.value.body;

        std::function<Result<Node>(std::string_view, ConfigFormat)> adapter_parse;
        std::string adapter_name;
        if (AcquireActiveAdapter(&adapter_parse, nullptr, &adapter_name) && adapter_parse)
        {
            const auto parsed = adapter_parse(text, format);
            if (parsed.ok)
            {
                return parsed;
            }
        }

        if (format == ConfigFormat::Json)
        {
            return ParseJson(text);
        }
        if (format == ConfigFormat::Ini)
        {
            return ParseIniText(text);
        }
        if (format == ConfigFormat::Yaml)
        {
            return ParseYamlText(text);
        }
        if (format == ConfigFormat::Toml)
        {
            return ParseTomlText(text);
        }

        return Result<Node>{false, Node{}, "unsupported config format in remote loader: " + std::string(ToString(format))};
    }

    namespace
    {

        Result<Node> ParseIniText(std::string_view text)
        {
            Node root = Node::MakeObject();
            std::string section;

            std::istringstream input{std::string(text)};
            std::string line;
            int line_no = 0;
            while (std::getline(input, line))
            {
                ++line_no;
                if (!line.empty() && line.back() == '\r')
                {
                    line.pop_back();
                }

                const std::string trimmed = TrimCopy(line);
                if (trimmed.empty() || trimmed[0] == ';' || trimmed[0] == '#')
                {
                    continue;
                }

                if (trimmed.front() == '[' && trimmed.back() == ']')
                {
                    section = TrimCopy(std::string_view(trimmed).substr(1, trimmed.size() - 2));
                    if (section.empty())
                    {
                        std::ostringstream err;
                        err << "invalid empty section name at line " << line_no;
                        return Result<Node>{false, Node{}, err.str()};
                    }
                    continue;
                }

                std::size_t split = trimmed.find('=');
                if (split == std::string::npos)
                {
                    split = trimmed.find(':');
                }
                if (split == std::string::npos)
                {
                    std::ostringstream err;
                    err << "invalid ini key/value at line " << line_no;
                    return Result<Node>{false, Node{}, err.str()};
                }

                const std::string key = TrimCopy(std::string_view(trimmed).substr(0, split));
                const std::string value_text = TrimCopy(std::string_view(trimmed).substr(split + 1));
                if (key.empty())
                {
                    std::ostringstream err;
                    err << "empty key at line " << line_no;
                    return Result<Node>{false, Node{}, err.str()};
                }

                const std::string path = section.empty() ? key : (section + "." + key);
                const auto st = SetNode(root, path, ParseScalar(value_text));
                if (!st.ok)
                {
                    std::ostringstream err;
                    err << "ini set failed at line " << line_no << ": " << st.error;
                    return Result<Node>{false, Node{}, err.str()};
                }
            }

            return Result<Node>{true, std::move(root), ""};
        }

        std::string EscapePathSegment(std::string_view segment)
        {
            std::string out;
            out.reserve(segment.size() + 4);
            for (char ch : segment)
            {
                if (ch == '\\' || ch == '.' || ch == '[' || ch == ']')
                {
                    out.push_back('\\');
                }
                out.push_back(ch);
            }
            return out;
        }

        std::string EscapeTomlString(std::string_view input)
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

        std::string ScalarToTomlText(const Node &value)
        {
            switch (value.Kind())
            {
            case NodeKind::Null:
                return "\"\"";
            case NodeKind::Bool:
                return value.AsBool() ? "true" : "false";
            case NodeKind::Integer:
                return std::to_string(value.AsInt());
            case NodeKind::Double:
            {
                std::ostringstream oss;
                oss << value.AsDouble();
                return oss.str();
            }
            case NodeKind::String:
                return "\"" + EscapeTomlString(value.AsString()) + "\"";
            case NodeKind::Array:
            case NodeKind::Object:
                return "\"" + EscapeTomlString(ToJson(value, -1)) + "\"";
            }
            return "\"\"";
        }

        Result<Node> ParseTomlText(std::string_view text)
        {
            Node root = Node::MakeObject();
            std::string section;

            std::istringstream input{std::string(text)};
            std::string line;
            int line_no = 0;
            while (std::getline(input, line))
            {
                ++line_no;
                if (!line.empty() && line.back() == '\r')
                {
                    line.pop_back();
                }

                const std::string trimmed = StripYamlComment(line);
                if (trimmed.empty())
                {
                    continue;
                }

                if (StartsWith(trimmed, "[[") && trimmed.size() >= 4 && trimmed.substr(trimmed.size() - 2) == "]]")
                {
                    std::ostringstream err;
                    err << "toml array-of-tables is not supported at line " << line_no;
                    return Result<Node>{false, Node{}, err.str()};
                }

                if (trimmed.front() == '[' && trimmed.back() == ']')
                {
                    section = TrimCopy(std::string_view(trimmed).substr(1, trimmed.size() - 2));
                    if (section.empty())
                    {
                        std::ostringstream err;
                        err << "invalid empty table at line " << line_no;
                        return Result<Node>{false, Node{}, err.str()};
                    }
                    continue;
                }

                const std::size_t split = trimmed.find('=');
                if (split == std::string::npos)
                {
                    std::ostringstream err;
                    err << "invalid toml key/value at line " << line_no;
                    return Result<Node>{false, Node{}, err.str()};
                }

                std::string key = TrimCopy(std::string_view(trimmed).substr(0, split));
                const std::string value_text = StripYamlComment(std::string_view(trimmed).substr(split + 1));
                if (key.empty())
                {
                    std::ostringstream err;
                    err << "empty key at line " << line_no;
                    return Result<Node>{false, Node{}, err.str()};
                }

                if ((key.size() >= 2 && key.front() == '"' && key.back() == '"') ||
                    (key.size() >= 2 && key.front() == '\'' && key.back() == '\''))
                {
                    key = key.substr(1, key.size() - 2);
                    key = EscapePathSegment(key);
                }

                const std::string path = section.empty() ? key : (section + "." + key);
                const auto st = SetNode(root, path, ParseScalar(value_text));
                if (!st.ok)
                {
                    std::ostringstream err;
                    err << "toml set failed at line " << line_no << ": " << st.error;
                    return Result<Node>{false, Node{}, err.str()};
                }
            }

            return Result<Node>{true, std::move(root), ""};
        }

        std::string DumpTomlText(const Node &root)
        {
            std::ostringstream out;

            const auto *obj = root.TryObject();
            if (obj == nullptr)
            {
                out << "value = " << ScalarToTomlText(root) << "\n";
                return out.str();
            }

            bool wrote_any = false;
            std::function<void(const Node::Object &, const std::string &, bool)> emit_object;
            emit_object = [&](const Node::Object &current, const std::string &prefix, bool print_header)
            {
                std::vector<std::pair<std::string, const Node *>> scalars;
                std::vector<std::pair<std::string, const Node::Object *>> children;
                scalars.reserve(current.size());
                children.reserve(current.size());

                for (const auto &entry : current)
                {
                    if (entry.second.Kind() == NodeKind::Object)
                    {
                        const auto *child_obj = entry.second.TryObject();
                        if (child_obj != nullptr)
                        {
                            children.push_back({entry.first, child_obj});
                        }
                    }
                    else
                    {
                        scalars.push_back({entry.first, &entry.second});
                    }
                }

                std::sort(scalars.begin(), scalars.end(), [](const auto &lhs, const auto &rhs)
                          { return lhs.first < rhs.first; });
                std::sort(children.begin(), children.end(), [](const auto &lhs, const auto &rhs)
                          { return lhs.first < rhs.first; });

                if (print_header)
                {
                    if (wrote_any)
                    {
                        out << "\n";
                    }
                    out << "[" << prefix << "]\n";
                    wrote_any = true;
                }

                for (const auto &entry : scalars)
                {
                    out << entry.first << " = " << ScalarToTomlText(*entry.second) << "\n";
                    wrote_any = true;
                }

                for (const auto &entry : children)
                {
                    const std::string child_prefix = prefix.empty() ? entry.first : (prefix + "." + entry.first);
                    emit_object(*entry.second, child_prefix, true);
                }
            };

            emit_object(*obj, "", false);
            return out.str();
        }

        struct YamlLine
        {
            int indent{0};
            int line_no{0};
            std::string text;
        };

        Result<Node> ParseYamlText(std::string_view text)
        {
            std::vector<YamlLine> lines;
            std::istringstream input{std::string(text)};
            std::string raw_line;
            int line_no = 0;

            while (std::getline(input, raw_line))
            {
                ++line_no;
                if (!raw_line.empty() && raw_line.back() == '\r')
                {
                    raw_line.pop_back();
                }

                if (raw_line.find('\t') != std::string::npos)
                {
                    std::ostringstream err;
                    err << "yaml tabs are not supported at line " << line_no;
                    return Result<Node>{false, Node{}, err.str()};
                }

                std::size_t offset = 0;
                while (offset < raw_line.size() && raw_line[offset] == ' ')
                {
                    ++offset;
                }

                const std::string payload = StripYamlComment(std::string_view(raw_line).substr(offset));
                if (payload.empty())
                {
                    continue;
                }

                YamlLine line;
                line.indent = static_cast<int>(offset);
                line.line_no = line_no;
                line.text = payload;
                lines.push_back(std::move(line));
            }

            if (lines.empty())
            {
                return Result<Node>{true, Node::MakeObject(), ""};
            }

            std::size_t index = 0;
            std::function<Result<Node>(int)> parse_block;
            parse_block = [&](int indent) -> Result<Node>
            {
                if (index >= lines.size())
                {
                    return Result<Node>{false, Node{}, "unexpected end of yaml"};
                }

                const bool sequence_mode = StartsWith(lines[index].text, "-");
                if (sequence_mode)
                {
                    Node::Array arr;
                    while (index < lines.size())
                    {
                        if (lines[index].indent < indent)
                        {
                            break;
                        }
                        if (lines[index].indent > indent)
                        {
                            std::ostringstream err;
                            err << "unexpected indentation at line " << lines[index].line_no;
                            return Result<Node>{false, Node{}, err.str()};
                        }

                        const std::string &line_text = lines[index].text;
                        if (!StartsWith(line_text, "-"))
                        {
                            std::ostringstream err;
                            err << "mixed mapping/sequence is unsupported at line " << lines[index].line_no;
                            return Result<Node>{false, Node{}, err.str()};
                        }

                        if (line_text.size() > 1 && line_text[1] != ' ')
                        {
                            std::ostringstream err;
                            err << "expected space after '-' at line " << lines[index].line_no;
                            return Result<Node>{false, Node{}, err.str()};
                        }

                        const std::string item_text = line_text.size() <= 1 ? "" : TrimCopy(std::string_view(line_text).substr(2));
                        ++index;

                        if (item_text.empty())
                        {
                            if (index < lines.size() && lines[index].indent > indent)
                            {
                                auto child = parse_block(lines[index].indent);
                                if (!child.ok)
                                {
                                    return child;
                                }
                                arr.push_back(std::move(child.value));
                            }
                            else
                            {
                                arr.emplace_back(Node());
                            }
                            continue;
                        }

                        const std::size_t colon = item_text.find(':');
                        if (colon != std::string::npos)
                        {
                            const std::string key = TrimCopy(std::string_view(item_text).substr(0, colon));
                            const std::string tail = TrimCopy(std::string_view(item_text).substr(colon + 1));
                            if (!key.empty())
                            {
                                Node::Object obj;
                                if (!tail.empty())
                                {
                                    obj.push_back({key, ParseScalar(tail)});
                                }
                                else if (index < lines.size() && lines[index].indent > indent)
                                {
                                    auto child = parse_block(lines[index].indent);
                                    if (!child.ok)
                                    {
                                        return child;
                                    }
                                    obj.push_back({key, std::move(child.value)});
                                }
                                else
                                {
                                    obj.push_back({key, Node()});
                                }

                                if (index < lines.size() && lines[index].indent > indent)
                                {
                                    auto extra = parse_block(lines[index].indent);
                                    if (!extra.ok)
                                    {
                                        return extra;
                                    }
                                    if (extra.value.Kind() != NodeKind::Object)
                                    {
                                        std::ostringstream err;
                                        err << "expected mapping continuation for list item at line " << lines[index - 1].line_no;
                                        return Result<Node>{false, Node{}, err.str()};
                                    }
                                    const auto *extra_obj = extra.value.TryObject();
                                    for (const auto &kv : *extra_obj)
                                    {
                                        obj.push_back(kv);
                                    }
                                }

                                arr.emplace_back(Node(std::move(obj)));
                                continue;
                            }
                        }

                        arr.emplace_back(ParseScalar(item_text));
                    }

                    return Result<Node>{true, Node(std::move(arr)), ""};
                }

                Node::Object obj;
                while (index < lines.size())
                {
                    if (lines[index].indent < indent)
                    {
                        break;
                    }
                    if (lines[index].indent > indent)
                    {
                        std::ostringstream err;
                        err << "unexpected indentation at line " << lines[index].line_no;
                        return Result<Node>{false, Node{}, err.str()};
                    }

                    const std::string &line_text = lines[index].text;
                    if (StartsWith(line_text, "-"))
                    {
                        std::ostringstream err;
                        err << "mixed mapping/sequence is unsupported at line " << lines[index].line_no;
                        return Result<Node>{false, Node{}, err.str()};
                    }

                    const std::size_t split = line_text.find(':');
                    if (split == std::string::npos)
                    {
                        std::ostringstream err;
                        err << "invalid yaml mapping item at line " << lines[index].line_no;
                        return Result<Node>{false, Node{}, err.str()};
                    }

                    const std::string key = TrimCopy(std::string_view(line_text).substr(0, split));
                    const std::string tail = TrimCopy(std::string_view(line_text).substr(split + 1));
                    if (key.empty())
                    {
                        std::ostringstream err;
                        err << "empty yaml key at line " << lines[index].line_no;
                        return Result<Node>{false, Node{}, err.str()};
                    }

                    ++index;
                    if (!tail.empty())
                    {
                        obj.push_back({key, ParseScalar(tail)});
                        continue;
                    }

                    if (index < lines.size() && lines[index].indent > indent)
                    {
                        auto child = parse_block(lines[index].indent);
                        if (!child.ok)
                        {
                            return child;
                        }
                        obj.push_back({key, std::move(child.value)});
                    }
                    else
                    {
                        obj.push_back({key, Node()});
                    }
                }

                return Result<Node>{true, Node(std::move(obj)), ""};
            };

            auto parsed = parse_block(lines[0].indent);
            if (!parsed.ok)
            {
                return parsed;
            }

            if (index != lines.size())
            {
                std::ostringstream err;
                err << "yaml parse did not consume all lines, stopped near line " << lines[index].line_no;
                return Result<Node>{false, Node{}, err.str()};
            }

            return parsed;
        }

        std::string DumpIniText(const Node &root)
        {
            struct FlatEntry
            {
                std::string path;
                const Node *value{nullptr};
            };

            std::vector<FlatEntry> leaves;
            std::function<void(const Node &, const std::string &)> collect;
            collect = [&](const Node &node, const std::string &path)
            {
                if (node.Kind() == NodeKind::Object)
                {
                    const auto *obj = node.TryObject();
                    for (const auto &kv : *obj)
                    {
                        const std::string next = path.empty() ? kv.first : (path + "." + kv.first);
                        collect(kv.second, next);
                    }
                    return;
                }

                leaves.push_back(FlatEntry{path, &node});
            };

            if (root.Kind() == NodeKind::Object)
            {
                collect(root, "");
            }
            else
            {
                leaves.push_back(FlatEntry{"value", &root});
            }

            std::vector<std::pair<std::string, std::string>> top_level;
            std::vector<std::pair<std::string, std::vector<std::pair<std::string, std::string>>>> sections;

            auto add_section_value = [&](const std::string &section_name, std::string key, std::string value)
            {
                for (auto &sec : sections)
                {
                    if (sec.first == section_name)
                    {
                        sec.second.push_back({std::move(key), std::move(value)});
                        return;
                    }
                }
                sections.push_back({section_name, {}});
                sections.back().second.push_back({std::move(key), std::move(value)});
            };

            for (const auto &entry : leaves)
            {
                std::string full = entry.path.empty() ? "value" : entry.path;
                const std::size_t dot = full.find('.');
                if (dot == std::string::npos)
                {
                    top_level.push_back({full, ScalarToIniText(*entry.value)});
                    continue;
                }

                const std::string section = full.substr(0, dot);
                const std::string key = full.substr(dot + 1);
                add_section_value(section, key, ScalarToIniText(*entry.value));
            }

            std::ostringstream oss;
            for (const auto &kv : top_level)
            {
                oss << kv.first << '=' << kv.second << '\n';
            }

            if (!top_level.empty() && !sections.empty())
            {
                oss << '\n';
            }

            for (std::size_t i = 0; i < sections.size(); ++i)
            {
                oss << '[' << sections[i].first << "]\n";
                for (const auto &kv : sections[i].second)
                {
                    oss << kv.first << '=' << kv.second << '\n';
                }
                if (i + 1 < sections.size())
                {
                    oss << '\n';
                }
            }

            return oss.str();
        }

        void AppendYamlNode(const Node &node, std::ostringstream &oss, int indent, int depth)
        {
            const auto pad = [&]()
            {
                for (int i = 0; i < indent * depth; ++i)
                {
                    oss << ' ';
                }
            };

            if (node.Kind() == NodeKind::Object)
            {
                const auto *obj = node.TryObject();
                for (const auto &kv : *obj)
                {
                    pad();
                    if (IsScalarNode(kv.second))
                    {
                        oss << kv.first << ": " << ScalarToYamlText(kv.second) << '\n';
                    }
                    else
                    {
                        oss << kv.first << ":\n";
                        AppendYamlNode(kv.second, oss, indent, depth + 1);
                    }
                }
                return;
            }

            if (node.Kind() == NodeKind::Array)
            {
                const auto *arr = node.TryArray();
                for (const auto &item : *arr)
                {
                    pad();
                    if (IsScalarNode(item))
                    {
                        oss << "- " << ScalarToYamlText(item) << '\n';
                    }
                    else
                    {
                        oss << "-\n";
                        AppendYamlNode(item, oss, indent, depth + 1);
                    }
                }
                return;
            }

            pad();
            oss << ScalarToYamlText(node) << '\n';
        }

        std::string DumpYamlText(const Node &root, int indent)
        {
            std::ostringstream oss;
            const int safe_indent = indent < 1 ? 2 : indent;
            AppendYamlNode(root, oss, safe_indent, 0);
            return oss.str();
        }

        struct FlatLeafEntry
        {
            std::string path;
            const Node *value{nullptr};
        };

        void CollectLeafEntries(const Node &node, const std::string &path, std::vector<FlatLeafEntry> &out)
        {
            if (node.Kind() == NodeKind::Object)
            {
                const auto *obj = node.TryObject();
                if (obj == nullptr)
                {
                    return;
                }

                for (const auto &kv : *obj)
                {
                    const std::string next = path.empty() ? kv.first : (path + "." + kv.first);
                    CollectLeafEntries(kv.second, next, out);
                }
                return;
            }

            if (node.Kind() == NodeKind::Array)
            {
                const auto *arr = node.TryArray();
                if (arr == nullptr)
                {
                    return;
                }

                for (std::size_t i = 0; i < arr->size(); ++i)
                {
                    const std::string next = path + "[" + std::to_string(i) + "]";
                    CollectLeafEntries((*arr)[i], next, out);
                }
                return;
            }

            out.push_back(FlatLeafEntry{path, &node});
        }

        std::vector<FlatLeafEntry> BuildLeafEntries(const Node &root)
        {
            std::vector<FlatLeafEntry> out;
            if (root.Kind() == NodeKind::Object)
            {
                CollectLeafEntries(root, "", out);
            }
            else if (root.Kind() == NodeKind::Array)
            {
                CollectLeafEntries(root, "", out);
            }
            else
            {
                out.push_back(FlatLeafEntry{"value", &root});
            }
            return out;
        }

        std::vector<std::string> SplitLines(std::string_view text)
        {
            std::vector<std::string> lines;
            std::istringstream input{std::string(text)};
            std::string line;
            while (std::getline(input, line))
            {
                if (!line.empty() && line.back() == '\r')
                {
                    line.pop_back();
                }
                lines.push_back(std::move(line));
            }

            if (lines.empty())
            {
                lines.push_back("");
            }
            return lines;
        }

        std::string JoinLines(const std::vector<std::string> &lines, bool trailing_newline)
        {
            std::ostringstream oss;
            for (std::size_t i = 0; i < lines.size(); ++i)
            {
                oss << lines[i];
                if (i + 1 < lines.size() || trailing_newline)
                {
                    oss << '\n';
                }
            }
            return oss.str();
        }

        struct ValueAndComment
        {
            std::string value;
            std::string suffix;
        };

        ValueAndComment SplitValueAndComment(std::string_view text)
        {
            bool in_single_quote = false;
            bool in_double_quote = false;
            std::size_t marker = std::string::npos;

            for (std::size_t i = 0; i < text.size(); ++i)
            {
                const char ch = text[i];
                if (ch == '\'' && !in_double_quote)
                {
                    in_single_quote = !in_single_quote;
                    continue;
                }
                if (ch == '"' && !in_single_quote)
                {
                    in_double_quote = !in_double_quote;
                    continue;
                }

                if ((ch == '#' || ch == ';') && !in_single_quote && !in_double_quote)
                {
                    if (i == 0 || std::isspace(static_cast<unsigned char>(text[i - 1])) != 0)
                    {
                        marker = i;
                        while (marker > 0 && std::isspace(static_cast<unsigned char>(text[marker - 1])) != 0)
                        {
                            --marker;
                        }
                        break;
                    }
                }
            }

            ValueAndComment out;
            if (marker == std::string::npos)
            {
                out.value = TrimCopy(text);
                return out;
            }

            out.value = TrimCopy(text.substr(0, marker));
            out.suffix = std::string(text.substr(marker));
            return out;
        }

        bool TryParseIniSection(std::string_view line, std::string &section)
        {
            const std::string trimmed = TrimCopy(line);
            if (trimmed.size() < 2 || trimmed.front() != '[' || trimmed.back() != ']')
            {
                return false;
            }

            section = TrimCopy(std::string_view(trimmed).substr(1, trimmed.size() - 2));
            return !section.empty();
        }

        struct IniKeyValueLine
        {
            std::string key;
            std::string prefix;
            std::string suffix;
        };

        std::optional<IniKeyValueLine> TryParseIniKeyValueLine(std::string_view line)
        {
            const std::string trimmed = TrimCopy(line);
            if (trimmed.empty() || trimmed[0] == ';' || trimmed[0] == '#')
            {
                return std::nullopt;
            }
            if (trimmed.front() == '[' && trimmed.back() == ']')
            {
                return std::nullopt;
            }

            const std::size_t eq = line.find('=');
            const std::size_t colon = line.find(':');
            std::size_t split = std::string::npos;
            if (eq != std::string::npos && colon != std::string::npos)
            {
                split = std::min(eq, colon);
            }
            else if (eq != std::string::npos)
            {
                split = eq;
            }
            else
            {
                split = colon;
            }

            if (split == std::string::npos)
            {
                return std::nullopt;
            }

            IniKeyValueLine out;
            out.key = TrimCopy(line.substr(0, split));
            if (out.key.empty())
            {
                return std::nullopt;
            }

            std::size_t value_start = split + 1;
            while (value_start < line.size() && std::isspace(static_cast<unsigned char>(line[value_start])) != 0)
            {
                ++value_start;
            }

            out.prefix = std::string(line.substr(0, value_start));
            const ValueAndComment split_value = SplitValueAndComment(line.substr(value_start));
            out.suffix = split_value.suffix;
            return out;
        }

        std::optional<std::string> TryRenderIniWithCommentPreserve(const Node &root, std::string_view original_text)
        {
            if (original_text.empty())
            {
                return std::nullopt;
            }

            auto lines = SplitLines(original_text);
            const bool trailing_newline = !original_text.empty() && original_text.back() == '\n';

            const std::vector<FlatLeafEntry> leaves = BuildLeafEntries(root);
            std::unordered_map<std::string, const Node *> leaf_map;
            leaf_map.reserve(leaves.size() * 2 + 1);
            for (const auto &entry : leaves)
            {
                const std::string normalized = entry.path.empty() ? "value" : entry.path;
                leaf_map.insert_or_assign(normalized, entry.value);
            }

            std::unordered_set<std::string> emitted;
            emitted.reserve(leaf_map.size() * 2 + 1);

            std::string section;
            for (auto &line : lines)
            {
                std::string parsed_section;
                if (TryParseIniSection(line, parsed_section))
                {
                    section = parsed_section;
                    continue;
                }

                const auto key_line = TryParseIniKeyValueLine(line);
                if (!key_line.has_value())
                {
                    continue;
                }

                const std::string path = section.empty() ? key_line->key : (section + "." + key_line->key);
                const auto it = leaf_map.find(path);
                if (it == leaf_map.end() || it->second == nullptr)
                {
                    continue;
                }

                line = key_line->prefix + ScalarToIniText(*it->second) + key_line->suffix;
                emitted.insert(path);
            }

            std::vector<std::pair<std::string, std::string>> missing_top;
            std::vector<std::pair<std::string, std::vector<std::pair<std::string, std::string>>>> missing_sections;
            auto add_missing_section = [&](const std::string &sec, std::string key, std::string value)
            {
                for (auto &entry : missing_sections)
                {
                    if (entry.first == sec)
                    {
                        entry.second.push_back({std::move(key), std::move(value)});
                        return;
                    }
                }
                missing_sections.push_back({sec, {}});
                missing_sections.back().second.push_back({std::move(key), std::move(value)});
            };

            for (const auto &entry : leaves)
            {
                const std::string path = entry.path.empty() ? "value" : entry.path;
                if (emitted.find(path) != emitted.end())
                {
                    continue;
                }

                const std::size_t dot = path.find('.');
                if (dot == std::string::npos)
                {
                    missing_top.push_back({path, ScalarToIniText(*entry.value)});
                    continue;
                }

                const std::string sec = path.substr(0, dot);
                const std::string key = path.substr(dot + 1);
                add_missing_section(sec, key, ScalarToIniText(*entry.value));
            }

            if (!missing_top.empty() || !missing_sections.empty())
            {
                if (!lines.empty() && !lines.back().empty())
                {
                    lines.push_back("");
                }

                for (const auto &kv : missing_top)
                {
                    lines.push_back(kv.first + "=" + kv.second);
                }

                if (!missing_top.empty() && !missing_sections.empty())
                {
                    lines.push_back("");
                }

                for (std::size_t i = 0; i < missing_sections.size(); ++i)
                {
                    const auto &sec = missing_sections[i];
                    lines.push_back("[" + sec.first + "]");

                    for (const auto &kv : sec.second)
                    {
                        lines.push_back(kv.first + "=" + kv.second);
                    }

                    if (i + 1 < missing_sections.size())
                    {
                        lines.push_back("");
                    }
                }
            }

            return JoinLines(lines, trailing_newline);
        }

        std::optional<std::string> TryRenderYamlWithCommentPreserve(const Node &root, std::string_view original_text)
        {
            if (original_text.empty())
            {
                return std::nullopt;
            }

            auto lines = SplitLines(original_text);
            const bool trailing_newline = !original_text.empty() && original_text.back() == '\n';

            const std::vector<FlatLeafEntry> leaves = BuildLeafEntries(root);
            std::unordered_map<std::string, const Node *> leaf_map;
            leaf_map.reserve(leaves.size() * 2 + 1);
            for (const auto &entry : leaves)
            {
                if (entry.path.empty())
                {
                    continue;
                }
                leaf_map.insert_or_assign(entry.path, entry.value);
            }

            std::unordered_set<std::string> emitted;
            emitted.reserve(leaf_map.size() * 2 + 1);

            struct StackItem
            {
                int indent{0};
                std::string path;
            };
            std::vector<StackItem> stack;

            for (auto &line : lines)
            {
                std::size_t offset = 0;
                while (offset < line.size() && line[offset] == ' ')
                {
                    ++offset;
                }

                if (offset >= line.size())
                {
                    continue;
                }

                const std::string payload = StripYamlComment(std::string_view(line).substr(offset));
                if (payload.empty())
                {
                    continue;
                }

                while (!stack.empty() && static_cast<int>(offset) <= stack.back().indent)
                {
                    stack.pop_back();
                }

                if (StartsWith(payload, "-"))
                {
                    continue;
                }

                const std::size_t split = payload.find(':');
                if (split == std::string::npos)
                {
                    continue;
                }

                const std::string key = TrimCopy(std::string_view(payload).substr(0, split));
                if (key.empty())
                {
                    continue;
                }

                const std::string value_tail = TrimCopy(std::string_view(payload).substr(split + 1));
                const std::string parent = stack.empty() ? "" : stack.back().path;
                const std::string path = parent.empty() ? key : (parent + "." + key);

                if (value_tail.empty())
                {
                    stack.push_back(StackItem{static_cast<int>(offset), path});
                    continue;
                }

                const auto found = leaf_map.find(path);
                if (found == leaf_map.end() || found->second == nullptr || !IsScalarNode(*found->second))
                {
                    continue;
                }

                const std::size_t raw_split = line.find(':', offset);
                if (raw_split == std::string::npos)
                {
                    continue;
                }

                std::size_t value_start = raw_split + 1;
                while (value_start < line.size() && line[value_start] == ' ')
                {
                    ++value_start;
                }

                const ValueAndComment split_value = SplitValueAndComment(std::string_view(line).substr(value_start));
                line = line.substr(0, value_start) + ScalarToYamlText(*found->second) + split_value.suffix;
                emitted.insert(path);
            }

            // For unsupported structural rewrites, fallback to full dump to avoid dropping user changes.
            for (const auto &entry : leaves)
            {
                if (entry.path.empty())
                {
                    continue;
                }
                if (entry.path.find('[') != std::string::npos)
                {
                    continue;
                }
                if (!IsScalarNode(*entry.value))
                {
                    continue;
                }

                if (emitted.find(entry.path) == emitted.end())
                {
                    return std::nullopt;
                }
            }

            return JoinLines(lines, trailing_newline);
        }

        std::uint32_t Fnv1a32(std::string_view text, std::uint32_t seed)
        {
            std::uint32_t hash = seed;
            for (unsigned char ch : text)
            {
                hash ^= static_cast<std::uint32_t>(ch);
                hash *= 16777619u;
            }
            return hash;
        }

        std::array<std::uint32_t, 4> DeriveTeaKey(std::string_view key_text)
        {
            return {
                Fnv1a32(key_text, 2166136261u),
                Fnv1a32(key_text, 2166136261u ^ 0x9e3779b9u),
                Fnv1a32(key_text, 2166136261u ^ 0x243f6a88u),
                Fnv1a32(key_text, 2166136261u ^ 0xb7e15162u)};
        }

        void TeaEncryptBlock(std::uint32_t &v0, std::uint32_t &v1, const std::array<std::uint32_t, 4> &key)
        {
            std::uint32_t sum = 0;
            constexpr std::uint32_t delta = 0x9e3779b9u;
            for (int i = 0; i < 32; ++i)
            {
                sum += delta;
                v0 += ((v1 << 4) + key[0]) ^ (v1 + sum) ^ ((v1 >> 5) + key[1]);
                v1 += ((v0 << 4) + key[2]) ^ (v0 + sum) ^ ((v0 >> 5) + key[3]);
            }
        }

        void ApplyTeaCtrXor(std::vector<std::uint8_t> &buffer,
                            const std::array<std::uint32_t, 4> &key,
                            std::uint64_t nonce)
        {
            std::uint64_t counter = 0;
            for (std::size_t offset = 0; offset < buffer.size(); offset += 8)
            {
                std::uint32_t v0 = static_cast<std::uint32_t>((nonce >> 32) & 0xFFFFFFFFu) ^
                                   static_cast<std::uint32_t>(counter & 0xFFFFFFFFu);
                std::uint32_t v1 = static_cast<std::uint32_t>(nonce & 0xFFFFFFFFu) ^
                                   static_cast<std::uint32_t>((counter >> 32) & 0xFFFFFFFFu);
                TeaEncryptBlock(v0, v1, key);

                std::array<std::uint8_t, 8> keystream = {
                    static_cast<std::uint8_t>((v0 >> 24) & 0xFFu),
                    static_cast<std::uint8_t>((v0 >> 16) & 0xFFu),
                    static_cast<std::uint8_t>((v0 >> 8) & 0xFFu),
                    static_cast<std::uint8_t>(v0 & 0xFFu),
                    static_cast<std::uint8_t>((v1 >> 24) & 0xFFu),
                    static_cast<std::uint8_t>((v1 >> 16) & 0xFFu),
                    static_cast<std::uint8_t>((v1 >> 8) & 0xFFu),
                    static_cast<std::uint8_t>(v1 & 0xFFu)};

                const std::size_t chunk = std::min<std::size_t>(8, buffer.size() - offset);
                for (std::size_t i = 0; i < chunk; ++i)
                {
                    buffer[offset + i] ^= keystream[i];
                }
                ++counter;
            }
        }

        std::string HexEncode(const std::vector<std::uint8_t> &data)
        {
            static constexpr char kHex[] = "0123456789abcdef";
            std::string out;
            out.reserve(data.size() * 2);
            for (std::uint8_t b : data)
            {
                out.push_back(kHex[(b >> 4) & 0x0F]);
                out.push_back(kHex[b & 0x0F]);
            }
            return out;
        }

        Result<std::vector<std::uint8_t>> HexDecode(std::string_view text)
        {
            auto hex_val = [](char ch) -> int
            {
                if (ch >= '0' && ch <= '9')
                {
                    return ch - '0';
                }
                if (ch >= 'a' && ch <= 'f')
                {
                    return 10 + (ch - 'a');
                }
                if (ch >= 'A' && ch <= 'F')
                {
                    return 10 + (ch - 'A');
                }
                return -1;
            };

            const std::string cleaned = TrimCopy(text);
            if (cleaned.size() % 2 != 0)
            {
                return Result<std::vector<std::uint8_t>>{false, {}, "invalid hex length"};
            }

            std::vector<std::uint8_t> out;
            out.reserve(cleaned.size() / 2);
            for (std::size_t i = 0; i < cleaned.size(); i += 2)
            {
                const int hi = hex_val(cleaned[i]);
                const int lo = hex_val(cleaned[i + 1]);
                if (hi < 0 || lo < 0)
                {
                    return Result<std::vector<std::uint8_t>>{false, {}, "invalid hex character"};
                }
                out.push_back(static_cast<std::uint8_t>((hi << 4) | lo));
            }

            return Result<std::vector<std::uint8_t>>{true, std::move(out), ""};
        }

        Result<Node> ParseTextByFormat(std::string_view text, ConfigFormat format)
        {
            std::function<Result<Node>(std::string_view, ConfigFormat)> adapter_parse;
            std::string adapter_name;
            if (AcquireActiveAdapter(&adapter_parse, nullptr, &adapter_name) && adapter_parse)
            {
                const auto parsed = adapter_parse(text, format);
                if (parsed.ok)
                {
                    return parsed;
                }
            }

            if (format == ConfigFormat::Json)
            {
                return ParseJson(text);
            }
            if (format == ConfigFormat::Ini)
            {
                return ParseIniText(text);
            }
            if (format == ConfigFormat::Yaml)
            {
                return ParseYamlText(text);
            }
            if (format == ConfigFormat::Toml)
            {
                return ParseTomlText(text);
            }

            return Result<Node>{false, Node{}, "unsupported config format in parser dispatch: " + std::string(ToString(format))};
        }

        Result<std::string> RenderTextByFormat(const Node &root, ConfigFormat format, int indent)
        {
            std::function<Result<std::string>(const Node &, ConfigFormat, int)> adapter_dump;
            std::string adapter_name;
            if (AcquireActiveAdapter(nullptr, &adapter_dump, &adapter_name) && adapter_dump)
            {
                const auto dumped = adapter_dump(root, format, indent);
                if (dumped.ok)
                {
                    return dumped;
                }
            }

            if (format == ConfigFormat::Json)
            {
                return Result<std::string>{true, ToJson(root, indent), ""};
            }
            if (format == ConfigFormat::Ini)
            {
                return Result<std::string>{true, DumpIniText(root), ""};
            }
            if (format == ConfigFormat::Yaml)
            {
                return Result<std::string>{true, DumpYamlText(root, indent), ""};
            }
            if (format == ConfigFormat::Toml)
            {
                return Result<std::string>{true, DumpTomlText(root), ""};
            }

            return Result<std::string>{false, "", "unsupported config format in render dispatch: " + std::string(ToString(format))};
        }

    } // namespace

    Result<Node> LoadFromFile(std::string_view file_path, ConfigFormat preferred)
    {
        const ConfigFormat format = (preferred == ConfigFormat::Unknown) ? DetectFormatFromPath(file_path) : preferred;
        if (format == ConfigFormat::Unknown)
        {
            return Result<Node>{false, Node{}, "cannot detect config format from file path"};
        }

        std::ifstream input(std::string(file_path), std::ios::binary);
        if (!input.is_open())
        {
            return Result<Node>{false, Node{}, "failed to open file: " + std::string(file_path)};
        }

        std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());

        std::function<Result<Node>(std::string_view, ConfigFormat)> adapter_parse;
        std::string adapter_name;
        if (AcquireActiveAdapter(&adapter_parse, nullptr, &adapter_name) && adapter_parse)
        {
            const auto parsed = adapter_parse(text, format);
            if (parsed.ok)
            {
                return parsed;
            }
        }

        if (format == ConfigFormat::Json)
        {
            return ParseJson(text);
        }
        if (format == ConfigFormat::Ini)
        {
            return ParseIniText(text);
        }
        if (format == ConfigFormat::Yaml)
        {
            return ParseYamlText(text);
        }
        if (format == ConfigFormat::Toml)
        {
            return ParseTomlText(text);
        }

        return Result<Node>{false, Node{}, "unsupported config format in file loader: " + std::string(ToString(format))};
    }

    Status SaveToFile(const Node &root, std::string_view file_path, ConfigFormat preferred, int indent)
    {
        const ConfigFormat format = (preferred == ConfigFormat::Unknown) ? DetectFormatFromPath(file_path) : preferred;
        if (format == ConfigFormat::Unknown)
        {
            return FailStatus("cannot detect config format from file path");
        }

        std::string existing_text;
        if (format == ConfigFormat::Ini || format == ConfigFormat::Yaml)
        {
            std::ifstream existing_input(std::string(file_path), std::ios::binary);
            if (existing_input.is_open())
            {
                existing_text.assign((std::istreambuf_iterator<char>(existing_input)), std::istreambuf_iterator<char>());
            }
        }

        std::string rendered_text;
        bool rendered_by_adapter = false;
        std::function<Result<std::string>(const Node &, ConfigFormat, int)> adapter_dump;
        std::string adapter_name;
        if (AcquireActiveAdapter(nullptr, &adapter_dump, &adapter_name) && adapter_dump)
        {
            const auto dumped = adapter_dump(root, format, indent);
            if (dumped.ok)
            {
                rendered_text = dumped.value;
                rendered_by_adapter = true;
            }
        }

        if (!rendered_by_adapter && format == ConfigFormat::Json)
        {
            rendered_text = ToJson(root, indent);
        }
        else if (!rendered_by_adapter && format == ConfigFormat::Ini)
        {
            const auto preserved = TryRenderIniWithCommentPreserve(root, existing_text);
            rendered_text = preserved.has_value() ? *preserved : DumpIniText(root);
        }
        else if (!rendered_by_adapter && format == ConfigFormat::Yaml)
        {
            const auto preserved = TryRenderYamlWithCommentPreserve(root, existing_text);
            rendered_text = preserved.has_value() ? *preserved : DumpYamlText(root, indent);
        }
        else if (!rendered_by_adapter && format == ConfigFormat::Toml)
        {
            rendered_text = DumpTomlText(root);
        }
        else if (!rendered_by_adapter)
        {
            return FailStatus("unsupported config format in file writer: " + std::string(ToString(format)));
        }

        std::ofstream output(std::string(file_path), std::ios::binary | std::ios::trunc);
        if (!output.is_open())
        {
            return FailStatus("failed to open file for write: " + std::string(file_path));
        }

        output << rendered_text;

        if (!output.good())
        {
            return FailStatus("failed while writing file: " + std::string(file_path));
        }
        return OkStatus();
    }

    Status SaveEncryptedToFile(const Node &root,
                               std::string_view file_path,
                               std::string_view key,
                               ConfigFormat preferred,
                               int indent)
    {
        if (TrimCopy(key).empty())
        {
            return FailStatus("encryption key must not be empty");
        }

        const ConfigFormat format = (preferred == ConfigFormat::Unknown) ? DetectFormatFromPath(file_path) : preferred;
        if (format == ConfigFormat::Unknown)
        {
            return FailStatus("cannot detect config format from file path");
        }

        const auto rendered = RenderTextByFormat(root, format, indent);
        if (!rendered.ok)
        {
            return FailStatus(rendered.error);
        }

        std::vector<std::uint8_t> bytes(rendered.value.begin(), rendered.value.end());
        const auto tea_key = DeriveTeaKey(key);
        const std::uint64_t nonce =
            static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()) ^
            static_cast<std::uint64_t>(std::hash<std::string_view>{}(file_path));
        ApplyTeaCtrXor(bytes, tea_key, nonce);

        std::ofstream output(std::string(file_path), std::ios::binary | std::ios::trunc);
        if (!output.is_open())
        {
            return FailStatus("failed to open file for write: " + std::string(file_path));
        }

        output << "CFGXENC1\n";
        output << "format=" << static_cast<int>(format) << "\n";
        output << "nonce=" << std::hex << nonce << std::dec << "\n";
        output << "data=" << HexEncode(bytes) << "\n";

        if (!output.good())
        {
            return FailStatus("failed while writing encrypted file: " + std::string(file_path));
        }

        return OkStatus();
    }

    Result<Node> LoadEncryptedFromFile(std::string_view file_path,
                                       std::string_view key,
                                       ConfigFormat preferred)
    {
        if (TrimCopy(key).empty())
        {
            return Result<Node>{false, Node{}, "encryption key must not be empty"};
        }

        std::ifstream input(std::string(file_path), std::ios::binary);
        if (!input.is_open())
        {
            return Result<Node>{false, Node{}, "failed to open file: " + std::string(file_path)};
        }

        std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        std::istringstream lines(text);
        std::string line;

        if (!std::getline(lines, line) || TrimCopy(line) != "CFGXENC1")
        {
            return Result<Node>{false, Node{}, "invalid encrypted config header"};
        }

        int format_id = static_cast<int>(ConfigFormat::Unknown);
        std::string nonce_text;
        std::string data_hex;

        while (std::getline(lines, line))
        {
            if (!line.empty() && line.back() == '\r')
            {
                line.pop_back();
            }

            const auto split = line.find('=');
            if (split == std::string::npos)
            {
                continue;
            }

            const std::string key_name = TrimCopy(line.substr(0, split));
            const std::string value = TrimCopy(line.substr(split + 1));
            if (key_name == "format")
            {
                format_id = std::atoi(value.c_str());
            }
            else if (key_name == "nonce")
            {
                nonce_text = value;
            }
            else if (key_name == "data")
            {
                data_hex = value;
            }
        }

        if (nonce_text.empty() || data_hex.empty())
        {
            return Result<Node>{false, Node{}, "incomplete encrypted config payload"};
        }

        const auto decoded = HexDecode(data_hex);
        if (!decoded.ok)
        {
            return Result<Node>{false, Node{}, "failed to decode encrypted payload: " + decoded.error};
        }

        char *nonce_end = nullptr;
        const std::uint64_t nonce = std::strtoull(nonce_text.c_str(), &nonce_end, 16);
        if (nonce_end == nullptr || *nonce_end != '\0')
        {
            return Result<Node>{false, Node{}, "invalid nonce field"};
        }

        std::vector<std::uint8_t> plain = decoded.value;
        const auto tea_key = DeriveTeaKey(key);
        ApplyTeaCtrXor(plain, tea_key, nonce);

        const std::string plain_text(plain.begin(), plain.end());

        ConfigFormat format = preferred;
        if (format == ConfigFormat::Unknown)
        {
            if (format_id < static_cast<int>(ConfigFormat::Unknown) || format_id > static_cast<int>(ConfigFormat::Toml))
            {
                return Result<Node>{false, Node{}, "invalid encrypted config format id"};
            }
            format = static_cast<ConfigFormat>(format_id);
        }

        return ParseTextByFormat(plain_text, format);
    }

} // namespace cfgx
