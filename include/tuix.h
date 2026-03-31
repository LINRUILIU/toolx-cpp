#pragma once

#include <cstdint>
#include <functional>
#include <iosfwd>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace tuix
{

    enum class Color
    {
        Default = -1,
        Black = 0,
        Red = 1,
        Green = 2,
        Yellow = 3,
        Blue = 4,
        Magenta = 5,
        Cyan = 6,
        White = 7,
        BrightBlack = 8,
        BrightRed = 9,
        BrightGreen = 10,
        BrightYellow = 11,
        BrightBlue = 12,
        BrightMagenta = 13,
        BrightCyan = 14,
        BrightWhite = 15
    };

    struct Position
    {
        std::uint16_t x{0};
        std::uint16_t y{0};
    };

    struct TerminalSize
    {
        std::uint16_t cols{0};
        std::uint16_t rows{0};
        std::uint16_t buffer_cols{0};
        std::uint16_t buffer_rows{0};
    };

    enum class CursorStyle
    {
        Block,
        Underline,
        Bar
    };

    enum class Key
    {
        Unknown = 0,
        Character,
        Enter,
        Escape,
        Tab,
        Backspace,
        ArrowUp,
        ArrowDown,
        ArrowLeft,
        ArrowRight,
        Home,
        End,
        PageUp,
        PageDown,
        Insert,
        Delete
    };

    enum class KeyModifier : std::uint8_t
    {
        None = 0,
        Shift = 1,
        Alt = 2,
        Ctrl = 4
    };

    inline KeyModifier operator|(KeyModifier a, KeyModifier b)
    {
        return static_cast<KeyModifier>(static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
    }

    inline KeyModifier &operator|=(KeyModifier &a, KeyModifier b)
    {
        a = a | b;
        return a;
    }

    inline bool HasModifier(KeyModifier value, KeyModifier flag)
    {
        return (static_cast<std::uint8_t>(value) & static_cast<std::uint8_t>(flag)) != 0;
    }

    enum class MouseButton
    {
        None,
        Left,
        Right,
        Middle
    };

    enum class MouseAction
    {
        None,
        Move,
        ButtonDown,
        ButtonUp,
        DoubleClick,
        Wheel
    };

    enum class EventType
    {
        None,
        Key,
        Mouse,
        Resize,
        Error
    };

    enum class PollStatus
    {
        NoEvent,
        HasEvent,
        Error
    };

    enum class InputConsumeMode
    {
        ExclusiveConsume,
        PeekOnly,
        TeeBack
    };

    struct KeyEvent
    {
        Key key{Key::Unknown};
        KeyModifier modifiers{KeyModifier::None};
        char ch{'\0'};
        std::string text;
    };

    struct MouseEvent
    {
        std::uint16_t x{0};
        std::uint16_t y{0};
        MouseButton button{MouseButton::None};
        MouseAction action{MouseAction::None};
        int wheel_delta{0};
        KeyModifier modifiers{KeyModifier::None};
    };

    struct ResizeEvent
    {
        TerminalSize size;
    };

    struct InputEvent
    {
        EventType type{EventType::None};
        KeyEvent key;
        MouseEvent mouse;
        ResizeEvent resize;
        std::string raw;
        std::string error;
    };

    struct PollResult
    {
        PollStatus status{PollStatus::NoEvent};
        InputEvent event;
        std::string message;
    };

    struct InputOptions
    {
        InputConsumeMode consume_mode{InputConsumeMode::ExclusiveConsume};
        bool enable_mouse{true};
        bool enable_resize_events{true};
    };

    class InputSource
    {
    public:
        virtual ~InputSource() = default;
        virtual PollResult Poll(int timeout_ms = 0) = 0;
        virtual bool SetConsumeMode(InputConsumeMode mode) = 0;
        virtual InputConsumeMode consume_mode() const noexcept = 0;
    };

    struct FrameCell
    {
        std::string utf8{" "};
        std::uint8_t display_width{1};
    };

    struct Rect
    {
        std::uint16_t x{0};
        std::uint16_t y{0};
        std::uint16_t width{0};
        std::uint16_t height{0};
    };

    class FrameBuffer
    {
    public:
        FrameBuffer(std::uint16_t width, std::uint16_t height, char fill = ' ');

        std::uint16_t width() const noexcept;
        std::uint16_t height() const noexcept;

        void Clear(char fill = ' ');
        bool Put(std::uint16_t x, std::uint16_t y, std::string_view utf8, std::uint8_t display_width = 1);
        const FrameCell *Get(std::uint16_t x, std::uint16_t y) const;

    private:
        std::uint16_t width_{0};
        std::uint16_t height_{0};
        std::vector<FrameCell> data_;
    };

    class Widget
    {
    public:
        virtual ~Widget() = default;
        virtual void Layout(const Rect &bounds);
        virtual void Render(FrameBuffer &frame) const = 0;
        virtual bool HandleEvent(const InputEvent &event);

        const Rect &bounds() const noexcept;

    private:
        Rect bounds_{};
    };

    class Layout : public Widget
    {
    public:
        void AddChild(std::shared_ptr<Widget> child);
        const std::vector<std::shared_ptr<Widget>> &Children() const noexcept;
        bool HandleEvent(const InputEvent &event) override;

    protected:
        std::vector<std::shared_ptr<Widget>> children_;
    };

    class VerticalLayout final : public Layout
    {
    public:
        void Layout(const Rect &bounds) override;
        void Render(FrameBuffer &frame) const override;
    };

    class HorizontalLayout final : public Layout
    {
    public:
        void Layout(const Rect &bounds) override;
        void Render(FrameBuffer &frame) const override;
    };

    class Label final : public Widget
    {
    public:
        explicit Label(std::string text = "");

        void SetText(std::string text);
        const std::string &text() const noexcept;

        void Render(FrameBuffer &frame) const override;

    private:
        std::string text_;
    };

    class Button final : public Widget
    {
    public:
        using ClickHandler = std::function<void()>;

        explicit Button(std::string text = "");

        void SetText(std::string text);
        const std::string &text() const noexcept;

        void SetFocused(bool focused) noexcept;
        bool focused() const noexcept;

        void SetOnClick(ClickHandler on_click);

        void Render(FrameBuffer &frame) const override;
        bool HandleEvent(const InputEvent &event) override;

    private:
        std::string text_;
        ClickHandler on_click_;
        bool focused_{false};
    };

    class Terminal
    {
    public:
        enum class Backend
        {
            None,
            Ansi,
            Win32
        };

        explicit Terminal(std::ostream &out, bool ansi_enabled = true);

        bool ClearScreen();
        bool MoveTo(std::uint16_t x, std::uint16_t y);
        bool SetCursorVisible(bool visible);
        bool SetCursorStyle(CursorStyle style);
        bool SetColor(Color fg = Color::Default, Color bg = Color::Default);
        bool ResetStyle();
        bool Print(std::string_view text);
        bool ClearLine();
        bool ClearToLineEnd();
        bool ClearRect(std::uint16_t x, std::uint16_t y, std::uint16_t width, std::uint16_t height);
        bool GetCursorPosition(Position *out_pos) const;
        TerminalSize GetSize() const;
        bool SetTitle(std::string_view title);
        std::size_t RenderFrameDiff(const FrameBuffer &frame,
                                    FrameBuffer *previous,
                                    std::uint16_t origin_x = 0,
                                    std::uint16_t origin_y = 0);
        bool Flush();
        Backend CurrentBackend() const noexcept;

    private:
        std::ostream *out_{nullptr};
        bool ansi_enabled_{true};
        bool win32_enabled_{false};
        std::uintptr_t win32_handle_{0};
        std::uint16_t win32_default_attr_{0};
    };

    class Application
    {
    public:
        explicit Application(Terminal *terminal = nullptr);

        void SetRoot(std::shared_ptr<Widget> root);
        void SetInputSource(std::unique_ptr<InputSource> input_source);

        void RequestExit() noexcept;
        bool Running() const noexcept;

        bool Tick(int timeout_ms = 0);
        int Run(int max_ticks = -1, int timeout_ms = 0);

    private:
        Terminal *terminal_{nullptr};
        std::shared_ptr<Widget> root_;
        std::unique_ptr<InputSource> input_source_;
        std::unique_ptr<FrameBuffer> previous_frame_;
        bool running_{true};
    };

    std::unique_ptr<InputSource> CreateConsoleInputSource(const InputOptions &options = {});
    std::unique_ptr<InputSource> CreateStreamInputSource(std::istream &in, const InputOptions &options = {});

} // namespace tuix
