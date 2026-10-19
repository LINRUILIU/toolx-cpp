# Early AI-use observation

> Audience: ToolX maintainers
> Status: Exploratory qualitative observation, not a token benchmark
> Applies to: v0.4 development; frozen ToolX baseline 24466b2
> Source of truth for: equal task preparation and observation boundaries

`prepare.py` creates two copies of the same sing-box task inputs and brief.
One fresh-context AI agent gets mature installed tools; another gets the same
facts/tools plus an optional frozen toolx-sync binary and its released docs.
Agents may choose their own solution, reuse their scripts, or reject ToolX.
They cannot inspect sibling solutions, prior experiments or the preparation
script. No task recipe or fault-location answer is provided.

The task is recurring configuration composition, diagnosis, actual application
acceptance, and local file publication. It includes a real configuration defect,
later scalar precedence, array order, a stale previous release, and required
invalid-config and publication-failure checks. Exact inputs and supplied facts
are frozen in `predeclared.json`. The old binary stays unchanged while contract
fixes are developed separately.

```powershell
python experiments/v04_ai_observation/prepare.py --root temp/ai-observation-new
```

Preparation does not call a model or run a task engine. Dispatch fresh agents
with only their brief, directory and condition, allowing ordinary sandbox
approval for local loopback/application execution. Keep scripts, raw output,
repair artifacts and an observation.json recording choices and friction.
Independently inspect outputs and run the actual application before interpreting
results. An action self-report is not a complete transcript or a token count.

The initial observation has one attempt per condition, the host's inherited
model, fixed condition order, and an isolated Windows workspace. It identifies
candidate obstacles; it cannot establish statistical effects, lower model costs,
blind evaluator independence or broad product preference. The actual model ID
and token accounting are unavailable and must not be invented.

No new configuration-explanation feature is a prerequisite. Record whether the
obstacle is an existing defect, capability discovery, project acceptance,
publication responsibility, or a genuinely missing tool capability. Keep an
unchanged released baseline distinct from subsequent fixed-version probes.
