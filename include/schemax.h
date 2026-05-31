#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "cfgx.h"

namespace schemax
{

struct Issue
{
    // Path uses cfgx dot/index notation without a leading "$", except root-only
    // issues which use "$".
    std::string path;
    // Stable MVP codes include: type, required, properties, items, minimum,
    // maximum, enum, minLength, maxLength, and additionalProperties.
    std::string code;
    std::string message;
};

struct Options
{
    // When true, validation stops after the first issue in deterministic visit
    // order. Compile errors are always fail-fast.
    bool fail_fast{false};
};

class Schema
{
  public:
    Schema() = default;
    explicit Schema(cfgx::Node root);

    const cfgx::Node& Root() const noexcept;

  private:
    cfgx::Node root_{cfgx::Node::MakeObject()};
};

// Compiles and lightly validates the schema document. This MVP accepts only the
// supported subset and returns ok=false for unknown keywords or malformed
// keyword values, instead of silently ignoring them.
cfgx::Result<Schema> Compile(const cfgx::Node& schema_root);

// Validates a cfgx document against a compiled schema. A successful validation
// returns an empty vector; validation issues are data, not transport errors.
std::vector<Issue> Validate(const cfgx::Node& document, const Schema& schema, const Options& options = {});

// Converts schemax issue details into cfgx::ValidationIssue for CLIs or code
// paths that already aggregate cfgx validation output.
std::vector<cfgx::ValidationIssue> ToCfgxIssues(const std::vector<Issue>& issues);

} // namespace schemax
