# tuix

> Audience: C++ tools needing deterministic terminal primitives
> Status: Experimental-foundation module guide
> Applies to: `v0.3.2` release candidate
> Source of truth for: current terminal building blocks and non-goals

## Role and boundary

`tuix` provides terminal capability detection, input events, styled frame cells,
diff rendering, layouts, a small widget set and a bounded application loop. It
is an experimental foundation, not a stable general TUI framework or terminal
emulator.

- CMake target: `toolx::tuix`
- Direct/transitive ToolX dependencies: none
- Stability: experimental foundation
- Header: [`include/tuix.h`](../../include/tuix.h)

## Quick start

```cpp
tuix::FrameBuffer frame(40, 5);
frame.PutStyled(1, 1, "ToolX", {tuix::Color::BrightCyan,
                                tuix::Color::Default, true});
const auto* cell = frame.Get(1, 1);
```

## Capability and API matrix

| Area | Public API | Notes |
| --- | --- | --- |
| Terminal model | `Terminal`, `TerminalSize`, `Position`, `Color`, `CursorStyle` | Backend capability varies by console/stream |
| Input | `InputSource`, `InputOptions`, `InputEvent`, key/mouse/resize enums and payloads | Consume modes report native/degraded/unsupported capability |
| Frames | `FrameBuffer`, `FrameCell`, `CellStyle`, `Theme` | Wide-glyph continuation cells support stable diff clearing |
| Geometry | `Rect`, `Insets` | Unsigned terminal coordinate model |
| Widget base | `Widget`, focus/visibility/enabled/event APIs | Layout/render/event surface is intentionally small |
| Layouts | `Layout`, `VerticalLayout`, `HorizontalLayout`, gap/padding/flex APIs | Stable deterministic remainder assignment within the MVP |
| Widgets | `Label`, `Panel`, `TextInput`, `ListView` | Small config-inspector-oriented set |
| Application | `Application` and render/input loop controls | Bounded loop for examples and `toolx-inspect` |

## Terminal and ownership behavior

- Widget trees use caller/shared ownership through `std::shared_ptr` where
  exposed by layouts.
- Themes are copied into widgets; later mutation of the source `Theme` does not
  update existing widgets.
- Actual input modes, ANSI support, Win32 console behavior, mouse and resize
  events depend on the connected terminal. Call capability queries rather than
  assuming an interactive console.
- Deterministic frame rendering can target an arbitrary output stream and is the
  preferred path for tests and documentation.
- `TextInput` behavior is byte-oriented within the current MVP; full Unicode
  grapheme editing is not promised.
- Real terminal state must be restored on normal shutdown; abrupt process
  termination remains an application concern.

## Stability, compatibility, examples, and version changes

The primitives are tested but experimental. `toolx-inspect` stabilizes its own
bounded report/render contract without stabilizing `tuix` as a framework.

No `tuix`-specific user-visible change is recorded for `v0.3.2`.

- [Annotated cookbook](../../examples/tuix_cookbook.cpp)
- [Basic terminal example](../../examples/tuix_example.cpp)
- [Widget showcase](../../examples/tuix_showcase.cpp)
- [Config inspector example](../../examples/tuix_config_inspector.cpp)
- [Behavior tests](../../tests/tuix_tests.cpp)
