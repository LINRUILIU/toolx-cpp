# ToolX Documentation

> Audience: users, C++ consumers, operators, and contributors
> Status: Canonical documentation portal
> Applies to: `v0.3.2` release candidate
> Source of truth for: document ownership and reading order

ToolX documentation is organized by reader intent. Public material is written
in English; development logs, design history, audits, and retrospectives are
maintained in Chinese.

## Recommended reading order

1. **First understanding:** read the repository [README](../README.md) for the
   current snapshot, value, non-goals and three five-minute paths.
2. **Architecture:** use [Architecture](architecture.md) for real CMake edges,
   module layers, product composition and the operator journey.
3. **Build:** choose a supported build shape in
   [Build and compatibility](build-and-compatibility.md), then verify dependency
   participation in [Dependencies](dependencies.md).
4. **Modules:** select a target from the [13-module index](modules/).
5. **CLI:** select a product from the [six-CLI index](cli/) and compare contracts
   in the [cross-CLI matrix](cli/matrix.md).
6. **Examples:** follow the [showcase portal](examples/showcase.md) from focused
   cookbooks to the complete product chain.
7. **Stability:** read the normative [Stability](stability.md) boundary before
   depending on experimental or backend-specific behavior.
8. **Development material:** contributors continue at the Chinese
   [development index](development/index.md).

## Public guides and specifications

| Document | Responsibility |
| --- | --- |
| [Architecture](architecture.md) | Module layers, CMake dependencies, CLI composition and data flow |
| [Build and compatibility](build-and-compatibility.md) | Supported build shapes, install/consume flow and verified environments |
| [Dependencies](dependencies.md) | Required, fetched, optional, system, development and packaged dependencies |
| [Stability](stability.md) | Normative stability levels, compatibility promises and version policy |
| [Roadmap](roadmap.md) | Current consolidation line and explicitly deferred work |
| [Changelog](../CHANGELOG.md) | User-visible feature and compatibility history |

## C++ module guides

| Stable core/support | Bounded or experimental |
| --- | --- |
| [argtool](modules/argtool.md) · [asyncx](modules/asyncx.md) · [cfgx](modules/cfgx.md) · [fsx](modules/fsx.md) · [logsys](modules/logsys.md) | [httpx](modules/httpx.md) — bounded stable |
| [utils](modules/utils.md) · [sysx](modules/sysx.md) · [resultx](modules/resultx.md) · [hashx](modules/hashx.md) · [textcodec](modules/textcodec.md) | [schemax](modules/schemax.md) — experimental MVP |
|  | [tuix](modules/tuix.md) — experimental foundation |

The [module index](modules/) compares all targets, dependencies, side effects,
and cookbook entry points.

## Product CLI references

- [toolx-config](cli/toolx-config.md)
- [toolx-sync](cli/toolx-sync.md)
- [toolx-pack](cli/toolx-pack.md)
- [toolx-http](cli/toolx-http.md)
- [toolx-log](cli/toolx-log.md)
- [toolx-inspect](cli/toolx-inspect.md)
- [Cross-CLI contract, side-effect and precedence matrix](cli/matrix.md)

## Examples and reports

- [Showcase overview](examples/showcase.md)
- [Module cookbook report](examples/module-cookbooks.md)
- [Reproducible product-chain walkthrough](examples/product-chain.md)

## Releases

- [v0.3.2 release-candidate notes](releases/v0.3.2.md)
- [v0.3.1](releases/v0.3.1.md)
- [v0.3.0](releases/v0.3.0.md)
- [v0.2.0](releases/v0.2.0.md)
- [v0.1.0](releases/v0.1.0.md)

## Development material

Maintainer-only material is indexed under
[docs/development](development/index.md). Historical documents are evidence and
context, not public compatibility specifications.
