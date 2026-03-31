#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

#include "tuix.h"

namespace
{
    struct DemoState
    {
        std::uint16_t cursor_x{10};
        std::uint16_t cursor_y{6};
        bool running{true};
        int frame_count{0};
        int event_count{0};
        std::string last_event{"(none)"};
        tuix::InputConsumeMode mode{tuix::InputConsumeMode::ExclusiveConsume};
        bool scripted_input{false};
    };

    std::uint16_t clamp_u16(int value, std::uint16_t lo, std::uint16_t hi)
    {
        if (value < static_cast<int>(lo))
        {
            return lo;
        }
        if (value > static_cast<int>(hi))
        {
            return hi;
        }
        return static_cast<std::uint16_t>(value);
    }

    std::string mode_name(tuix::InputConsumeMode mode)
    {
        switch (mode)
        {
        case tuix::InputConsumeMode::ExclusiveConsume:
            return "exclusive";
        case tuix::InputConsumeMode::PeekOnly:
            return "peek";
        case tuix::InputConsumeMode::TeeBack:
            return "teeback";
        }
        return "unknown";
    }

    tuix::InputConsumeMode next_mode(tuix::InputConsumeMode mode)
    {
        switch (mode)
        {
        case tuix::InputConsumeMode::ExclusiveConsume:
            return tuix::InputConsumeMode::PeekOnly;
        case tuix::InputConsumeMode::PeekOnly:
            return tuix::InputConsumeMode::TeeBack;
        case tuix::InputConsumeMode::TeeBack:
            return tuix::InputConsumeMode::ExclusiveConsume;
        }
        return tuix::InputConsumeMode::ExclusiveConsume;
    }

    std::size_t utf8_char_len(unsigned char c)
    {
        if ((c & 0x80u) == 0)
        {
            return 1;
        }
        if ((c & 0xE0u) == 0xC0u)
        {
            return 2;
        }
        if ((c & 0xF0u) == 0xE0u)
        {
            return 3;
        }
        if ((c & 0xF8u) == 0xF0u)
        {
            return 4;
        }
        return 1;
    }

    void put_text(tuix::FrameBuffer &fb, std::uint16_t x, std::uint16_t y, std::string_view text)
    {
        if (y >= fb.height())
        {
            return;
        }

        std::uint16_t col = x;
        std::size_t i = 0;
        while (i < text.size() && col < fb.width())
        {
            const unsigned char lead = static_cast<unsigned char>(text[i]);
            std::size_t len = utf8_char_len(lead);
            if (i + len > text.size())
            {
                len = 1;
            }

            const std::string_view ch(text.data() + i, len);
            if (!fb.Put(col, y, ch, 0))
            {
                break;
            }

            const tuix::FrameCell *cell = fb.Get(col, y);
            const std::uint16_t w = static_cast<std::uint16_t>((cell != nullptr && cell->display_width > 1) ? cell->display_width : 1);
            col = static_cast<std::uint16_t>(col + w);
            i += len;
        }
    }

    void draw_box(tuix::FrameBuffer &fb)
    {
        if (fb.width() < 2 || fb.height() < 2)
        {
            return;
        }

        const std::uint16_t max_x = static_cast<std::uint16_t>(fb.width() - 1);
        const std::uint16_t max_y = static_cast<std::uint16_t>(fb.height() - 1);

        fb.Put(0, 0, "+", 1);
        fb.Put(max_x, 0, "+", 1);
        fb.Put(0, max_y, "+", 1);
        fb.Put(max_x, max_y, "+", 1);

        for (std::uint16_t x = 1; x < max_x; ++x)
        {
            fb.Put(x, 0, "-", 1);
            fb.Put(x, max_y, "-", 1);
        }
        for (std::uint16_t y = 1; y < max_y; ++y)
        {
            fb.Put(0, y, "|", 1);
            fb.Put(max_x, y, "|", 1);
        }
    }

    void handle_key_event(const tuix::KeyEvent &key, DemoState *state, std::uint16_t width, std::uint16_t height)
    {
        if (state == nullptr)
        {
            return;
        }

        const std::uint16_t min_x = (width > 2) ? 1 : 0;
        const std::uint16_t max_x = (width > 2) ? static_cast<std::uint16_t>(width - 2) : 0;
        const std::uint16_t min_y = (height > 4) ? 3 : 0;
        const std::uint16_t max_y = (height > 2) ? static_cast<std::uint16_t>(height - 2) : 0;

        switch (key.key)
        {
        case tuix::Key::ArrowLeft:
            state->cursor_x = clamp_u16(static_cast<int>(state->cursor_x) - 1, min_x, max_x);
            state->last_event = "key:left";
            break;
        case tuix::Key::ArrowRight:
            state->cursor_x = clamp_u16(static_cast<int>(state->cursor_x) + 1, min_x, max_x);
            state->last_event = "key:right";
            break;
        case tuix::Key::ArrowUp:
            state->cursor_y = clamp_u16(static_cast<int>(state->cursor_y) - 1, min_y, max_y);
            state->last_event = "key:up";
            break;
        case tuix::Key::ArrowDown:
            state->cursor_y = clamp_u16(static_cast<int>(state->cursor_y) + 1, min_y, max_y);
            state->last_event = "key:down";
            break;
        case tuix::Key::Escape:
            state->last_event = "key:esc";
            state->running = false;
            break;
        case tuix::Key::Character:
            if (key.ch == 'q' || key.ch == 'Q')
            {
                state->last_event = "key:q";
                state->running = false;
            }
            else if (key.ch == 'a' || key.ch == 'A')
            {
                state->cursor_x = clamp_u16(static_cast<int>(state->cursor_x) - 1, min_x, max_x);
                state->last_event = "key:a";
            }
            else if (key.ch == 'd' || key.ch == 'D')
            {
                state->cursor_x = clamp_u16(static_cast<int>(state->cursor_x) + 1, min_x, max_x);
                state->last_event = "key:d";
            }
            else if (key.ch == 'w' || key.ch == 'W')
            {
                state->cursor_y = clamp_u16(static_cast<int>(state->cursor_y) - 1, min_y, max_y);
                state->last_event = "key:w";
            }
            else if (key.ch == 's' || key.ch == 'S')
            {
                state->cursor_y = clamp_u16(static_cast<int>(state->cursor_y) + 1, min_y, max_y);
                state->last_event = "key:s";
            }
            else
            {
                state->last_event = std::string("key:") + key.ch;
            }
            break;
        default:
            state->last_event = "key:other";
            break;
        }
    }

    void draw_ui(tuix::FrameBuffer &fb, const DemoState &state, const tuix::Terminal::Backend backend)
    {
        fb.Clear(' ');
        draw_box(fb);

        put_text(fb, 2, 0, "tuix showcase | arrows/WASD move | tab mode | q/esc quit");
        put_text(fb, 2, 1, std::string("backend=") + (backend == tuix::Terminal::Backend::Win32 ? "win32" : (backend == tuix::Terminal::Backend::Ansi ? "ansi" : "none")));
        put_text(fb, 2, 2, std::string("consume_mode=") + mode_name(state.mode) + " | events=" + std::to_string(state.event_count) + " | frames=" + std::to_string(state.frame_count));

        if (fb.height() > 3)
        {
            put_text(fb, 2, static_cast<std::uint16_t>(fb.height() - 2), std::string("last=") + state.last_event + " | width-demo: A中B");
        }

        if (state.cursor_x < fb.width() && state.cursor_y < fb.height())
        {
            fb.Put(state.cursor_x, state.cursor_y, "@", 1);
        }

        if (state.cursor_x + 1 < fb.width() && state.cursor_y < fb.height())
        {
            fb.Put(static_cast<std::uint16_t>(state.cursor_x + 1), state.cursor_y, "中", 0);
        }
    }
}

int main()
{
    system("chcp 65001 >nul");
    tuix::Terminal term(std::cout, true);
    term.ClearScreen();
    term.SetCursorVisible(false);
    term.SetTitle("tuix showcase");

    tuix::InputOptions options;
    options.consume_mode = tuix::InputConsumeMode::ExclusiveConsume;
    options.enable_mouse = true;
    options.enable_resize_events = true;

    std::istringstream scripted("dssdwwa\x1B[A\x1B[B\tq");
    std::unique_ptr<tuix::InputSource> input = tuix::CreateConsoleInputSource(options);

    DemoState state;
    if (!input)
    {
        state.scripted_input = true;
        input = tuix::CreateStreamInputSource(scripted, options);
        state.last_event = "console input unavailable, fallback to scripted stream";
    }

    tuix::TerminalSize size = term.GetSize();
    std::uint16_t width = size.cols > 0 ? size.cols : 100;
    std::uint16_t height = size.rows > 0 ? size.rows : 30;
    width = std::max<std::uint16_t>(width, 40);
    height = std::max<std::uint16_t>(height, 12);

    tuix::FrameBuffer previous(width, height, ' ');
    tuix::FrameBuffer current(width, height, ' ');

    state.cursor_x = clamp_u16(state.cursor_x, 1, static_cast<std::uint16_t>(width - 2));
    state.cursor_y = clamp_u16(state.cursor_y, 3, static_cast<std::uint16_t>(height - 2));

    while (state.running)
    {
        const tuix::PollResult polled = input->Poll(0);
        if (polled.status == tuix::PollStatus::HasEvent)
        {
            ++state.event_count;
            if (polled.event.type == tuix::EventType::Key)
            {
                if (polled.event.key.key == tuix::Key::Tab)
                {
                    state.mode = next_mode(state.mode);
                    input->SetConsumeMode(state.mode);
                    state.last_event = std::string("mode->") + mode_name(state.mode);
                }
                else
                {
                    handle_key_event(polled.event.key, &state, width, height);
                }
            }
            else if (polled.event.type == tuix::EventType::Mouse)
            {
                const std::uint16_t min_x = (width > 2) ? 1 : 0;
                const std::uint16_t max_x = (width > 2) ? static_cast<std::uint16_t>(width - 2) : 0;
                const std::uint16_t min_y = (height > 4) ? 3 : 0;
                const std::uint16_t max_y = (height > 2) ? static_cast<std::uint16_t>(height - 2) : 0;
                state.cursor_x = clamp_u16(polled.event.mouse.x, min_x, max_x);
                state.cursor_y = clamp_u16(polled.event.mouse.y, min_y, max_y);
                state.last_event = "mouse";
            }
            else if (polled.event.type == tuix::EventType::Resize)
            {
                if (polled.event.resize.size.cols >= 20 && polled.event.resize.size.rows >= 10)
                {
                    width = polled.event.resize.size.cols;
                    height = polled.event.resize.size.rows;
                    previous = tuix::FrameBuffer(width, height, ' ');
                    current = tuix::FrameBuffer(width, height, ' ');
                    state.cursor_x = clamp_u16(state.cursor_x, 1, static_cast<std::uint16_t>(width - 2));
                    state.cursor_y = clamp_u16(state.cursor_y, 3, static_cast<std::uint16_t>(height - 2));
                    state.last_event = "resize";
                    term.ClearScreen();
                }
            }
            if (!polled.message.empty())
            {
                state.last_event = polled.message;
            }
        }
        else if (polled.status == tuix::PollStatus::Error)
        {
            state.last_event = polled.message.empty() ? "input error" : polled.message;
        }

        draw_ui(current, state, term.CurrentBackend());

        term.SetColor(tuix::Color::BrightWhite, tuix::Color::Default);
        term.RenderFrameDiff(current, &previous, 0, 0);
        term.SetColor(tuix::Color::BrightCyan, tuix::Color::Default);
        term.MoveTo(2, 0);
        term.Print("tuix showcase");
        term.ResetStyle();
        term.Flush();

        ++state.frame_count;
        if (state.scripted_input && state.frame_count > 120)
        {
            state.running = false;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    term.ResetStyle();
    term.SetCursorVisible(true);
    term.MoveTo(0, static_cast<std::uint16_t>(height > 0 ? height - 1 : 0));
    term.Print("\n");
    term.Flush();
    return 0;
}
