#include "tuix.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <istream>
#include <iostream>
#include <ostream>
#include <thread>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#endif

namespace tuix
{
    namespace
    {

        int FgCode(Color c)
        {
            const int v = static_cast<int>(c);
            if (v < 0)
            {
                return 39;
            }
            if (v <= 7)
            {
                return 30 + v;
            }
            return 90 + (v - 8);
        }

        int BgCode(Color c)
        {
            const int v = static_cast<int>(c);
            if (v < 0)
            {
                return 49;
            }
            if (v <= 7)
            {
                return 40 + v;
            }
            return 100 + (v - 8);
        }

        bool IsAsciiLetter(char c)
        {
            return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
        }

        bool DecodeUtf8Codepoint(std::string_view text, std::size_t *index, std::uint32_t *codepoint)
        {
            if (index == nullptr || codepoint == nullptr || *index >= text.size())
            {
                return false;
            }

            const unsigned char c0 = static_cast<unsigned char>(text[*index]);
            if ((c0 & 0x80u) == 0)
            {
                *codepoint = c0;
                *index += 1;
                return true;
            }

            if ((c0 & 0xE0u) == 0xC0u)
            {
                if (*index + 1 >= text.size())
                {
                    return false;
                }
                const unsigned char c1 = static_cast<unsigned char>(text[*index + 1]);
                if ((c1 & 0xC0u) != 0x80u)
                {
                    return false;
                }
                *codepoint = (static_cast<std::uint32_t>(c0 & 0x1Fu) << 6) | static_cast<std::uint32_t>(c1 & 0x3Fu);
                *index += 2;
                return true;
            }

            if ((c0 & 0xF0u) == 0xE0u)
            {
                if (*index + 2 >= text.size())
                {
                    return false;
                }
                const unsigned char c1 = static_cast<unsigned char>(text[*index + 1]);
                const unsigned char c2 = static_cast<unsigned char>(text[*index + 2]);
                if ((c1 & 0xC0u) != 0x80u || (c2 & 0xC0u) != 0x80u)
                {
                    return false;
                }
                *codepoint = (static_cast<std::uint32_t>(c0 & 0x0Fu) << 12) |
                             (static_cast<std::uint32_t>(c1 & 0x3Fu) << 6) |
                             static_cast<std::uint32_t>(c2 & 0x3Fu);
                *index += 3;
                return true;
            }

            if ((c0 & 0xF8u) == 0xF0u)
            {
                if (*index + 3 >= text.size())
                {
                    return false;
                }
                const unsigned char c1 = static_cast<unsigned char>(text[*index + 1]);
                const unsigned char c2 = static_cast<unsigned char>(text[*index + 2]);
                const unsigned char c3 = static_cast<unsigned char>(text[*index + 3]);
                if ((c1 & 0xC0u) != 0x80u || (c2 & 0xC0u) != 0x80u || (c3 & 0xC0u) != 0x80u)
                {
                    return false;
                }
                *codepoint = (static_cast<std::uint32_t>(c0 & 0x07u) << 18) |
                             (static_cast<std::uint32_t>(c1 & 0x3Fu) << 12) |
                             (static_cast<std::uint32_t>(c2 & 0x3Fu) << 6) |
                             static_cast<std::uint32_t>(c3 & 0x3Fu);
                *index += 4;
                return true;
            }

            return false;
        }

        bool IsCombiningCodepoint(std::uint32_t cp)
        {
            return (cp >= 0x0300 && cp <= 0x036F) ||
                   (cp >= 0x1AB0 && cp <= 0x1AFF) ||
                   (cp >= 0x1DC0 && cp <= 0x1DFF) ||
                   (cp >= 0x20D0 && cp <= 0x20FF) ||
                   (cp >= 0xFE20 && cp <= 0xFE2F);
        }

        bool IsWideCodepoint(std::uint32_t cp)
        {
            return (cp >= 0x1100 && cp <= 0x115F) ||
                   (cp >= 0x2E80 && cp <= 0xA4CF) ||
                   (cp >= 0xAC00 && cp <= 0xD7A3) ||
                   (cp >= 0xF900 && cp <= 0xFAFF) ||
                   (cp >= 0xFE10 && cp <= 0xFE6F) ||
                   (cp >= 0xFF01 && cp <= 0xFF60) ||
                   (cp >= 0xFFE0 && cp <= 0xFFE6) ||
                   (cp >= 0x1F300 && cp <= 0x1FAFF);
        }

        std::uint8_t EstimateDisplayWidth(std::string_view text)
        {
            if (text.empty())
            {
                return 1;
            }

            std::size_t i = 0;
            int width = 0;
            while (i < text.size())
            {
                std::size_t prev = i;
                std::uint32_t cp = 0;
                if (!DecodeUtf8Codepoint(text, &i, &cp))
                {
                    i = prev + 1;
                    width += 1;
                    continue;
                }

                if ((cp <= 0x1F) || cp == 0x7F)
                {
                    continue;
                }
                if (IsCombiningCodepoint(cp))
                {
                    continue;
                }
                width += IsWideCodepoint(cp) ? 2 : 1;
            }

            if (width <= 0)
            {
                return 1;
            }
            if (width > 255)
            {
                return 255;
            }
            return static_cast<std::uint8_t>(width);
        }

        std::size_t Utf8CharLen(unsigned char c)
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

        void DrawUtf8Clipped(FrameBuffer &frame, const Rect &bounds, std::string_view text)
        {
            if (bounds.width == 0 || bounds.height == 0)
            {
                return;
            }

            std::uint16_t col = bounds.x;
            std::size_t i = 0;
            while (i < text.size() && col < static_cast<std::uint16_t>(bounds.x + bounds.width))
            {
                const unsigned char lead = static_cast<unsigned char>(text[i]);
                std::size_t len = Utf8CharLen(lead);
                if (i + len > text.size())
                {
                    len = 1;
                }

                const std::string_view ch(text.data() + i, len);
                const std::uint8_t width = EstimateDisplayWidth(ch);
                const std::uint16_t safe_width = std::max<std::uint16_t>(1, width);
                if (col + safe_width > static_cast<std::uint16_t>(bounds.x + bounds.width))
                {
                    break;
                }

                frame.Put(col, bounds.y, ch, width);
                col = static_cast<std::uint16_t>(col + safe_width);
                i += len;
            }
        }

        std::uint16_t DisplayWidthOrOne(const FrameCell &cell)
        {
            return std::max<std::uint16_t>(1, cell.display_width);
        }

        std::uint16_t FindCellAnchorX(const FrameBuffer &frame, std::uint16_t x, std::uint16_t y)
        {
            std::uint16_t anchor = x;
            while (anchor > 0)
            {
                const FrameCell *cell = frame.Get(anchor, y);
                if (cell == nullptr || !cell->continuation)
                {
                    break;
                }
                --anchor;
            }
            return anchor;
        }

        std::uint16_t AdvanceDisplayCell(const FrameBuffer &frame, std::uint16_t x, std::uint16_t y)
        {
            const FrameCell *cell = frame.Get(x, y);
            if (cell == nullptr)
            {
                return static_cast<std::uint16_t>(x + 1);
            }
            if (cell->continuation)
            {
                return static_cast<std::uint16_t>(x + 1);
            }
            return static_cast<std::uint16_t>(x + DisplayWidthOrOne(*cell));
        }

        bool CellsEquivalent(const FrameCell *a, const FrameCell *b)
        {
            if (a == nullptr || b == nullptr)
            {
                return a == b;
            }
            return a->utf8 == b->utf8 &&
                   a->display_width == b->display_width &&
                   a->continuation == b->continuation;
        }

        Widget *FindFocusedWidget(Widget *widget)
        {
            if (widget == nullptr || !widget->visible() || !widget->enabled())
            {
                return nullptr;
            }
            if (widget->focused())
            {
                return widget;
            }
            auto *layout = dynamic_cast<Layout *>(widget);
            if (layout == nullptr)
            {
                return nullptr;
            }
            for (const auto &child : layout->Children())
            {
                Widget *found = FindFocusedWidget(child.get());
                if (found != nullptr)
                {
                    return found;
                }
            }
            return nullptr;
        }

        void CollectFocusableWidgets(Widget *widget, std::vector<Widget *> *out)
        {
            if (widget == nullptr || out == nullptr || !widget->visible() || !widget->enabled())
            {
                return;
            }
            if (widget->focusable())
            {
                out->push_back(widget);
            }
            auto *layout = dynamic_cast<Layout *>(widget);
            if (layout == nullptr)
            {
                return;
            }
            for (const auto &child : layout->Children())
            {
                CollectFocusableWidgets(child.get(), out);
            }
        }

        Widget *FindWidgetAt(Widget *widget, std::uint16_t x, std::uint16_t y)
        {
            if (widget == nullptr || !widget->visible() || !widget->enabled() || !widget->HitTest(x, y))
            {
                return nullptr;
            }

            auto *layout = dynamic_cast<Layout *>(widget);
            if (layout != nullptr)
            {
                const auto &children = layout->Children();
                for (auto it = children.rbegin(); it != children.rend(); ++it)
                {
                    Widget *found = FindWidgetAt(it->get(), x, y);
                    if (found != nullptr)
                    {
                        return found;
                    }
                }
            }
            return widget;
        }

        bool IsCsiArrowWithModifier(std::string_view raw, char dir, int *mod)
        {
            if (mod == nullptr)
            {
                return false;
            }

            if (raw.size() < 6 || raw[0] != '\x1B' || raw[1] != '[')
            {
                return false;
            }

            if (raw.substr(2, 2) != "1;")
            {
                return false;
            }

            if (raw.back() != dir)
            {
                return false;
            }

            int value = 0;
            for (std::size_t i = 4; i + 1 < raw.size(); ++i)
            {
                if (!std::isdigit(static_cast<unsigned char>(raw[i])))
                {
                    return false;
                }
                value = value * 10 + (raw[i] - '0');
            }

            *mod = value;
            return true;
        }

        KeyModifier DecodeModifier(int csi_mod)
        {
            if (csi_mod <= 1)
            {
                return KeyModifier::None;
            }

            const int mask = csi_mod - 1;
            KeyModifier out = KeyModifier::None;
            if ((mask & 1) != 0)
            {
                out |= KeyModifier::Shift;
            }
            if ((mask & 2) != 0)
            {
                out |= KeyModifier::Alt;
            }
            if ((mask & 4) != 0)
            {
                out |= KeyModifier::Ctrl;
            }
            return out;
        }

        InputEvent ParseEscapedInput(const std::string &raw)
        {
            InputEvent ev;
            ev.raw = raw;

            if (raw.empty())
            {
                ev.type = EventType::None;
                return ev;
            }

            if (raw == "\x1B[A")
            {
                ev.type = EventType::Key;
                ev.key.key = Key::ArrowUp;
                return ev;
            }
            if (raw == "\x1B[B")
            {
                ev.type = EventType::Key;
                ev.key.key = Key::ArrowDown;
                return ev;
            }
            if (raw == "\x1B[C")
            {
                ev.type = EventType::Key;
                ev.key.key = Key::ArrowRight;
                return ev;
            }
            if (raw == "\x1B[D")
            {
                ev.type = EventType::Key;
                ev.key.key = Key::ArrowLeft;
                return ev;
            }
            if (raw == "\x1B[Z")
            {
                ev.type = EventType::Key;
                ev.key.key = Key::Tab;
                ev.key.modifiers = KeyModifier::Shift;
                return ev;
            }

            int mod = 0;
            if (IsCsiArrowWithModifier(raw, 'A', &mod))
            {
                ev.type = EventType::Key;
                ev.key.key = Key::ArrowUp;
                ev.key.modifiers = DecodeModifier(mod);
                return ev;
            }
            if (IsCsiArrowWithModifier(raw, 'B', &mod))
            {
                ev.type = EventType::Key;
                ev.key.key = Key::ArrowDown;
                ev.key.modifiers = DecodeModifier(mod);
                return ev;
            }
            if (IsCsiArrowWithModifier(raw, 'C', &mod))
            {
                ev.type = EventType::Key;
                ev.key.key = Key::ArrowRight;
                ev.key.modifiers = DecodeModifier(mod);
                return ev;
            }
            if (IsCsiArrowWithModifier(raw, 'D', &mod))
            {
                ev.type = EventType::Key;
                ev.key.key = Key::ArrowLeft;
                ev.key.modifiers = DecodeModifier(mod);
                return ev;
            }

            if (raw.size() == 1)
            {
                const char c = raw[0];
                ev.type = EventType::Key;
                if (c == '\n' || c == '\r')
                {
                    ev.key.key = Key::Enter;
                    return ev;
                }
                if (c == '\t')
                {
                    ev.key.key = Key::Tab;
                    return ev;
                }
                if (c == '\x1B')
                {
                    ev.key.key = Key::Escape;
                    return ev;
                }
                if (c == '\x08' || c == '\x7F')
                {
                    ev.key.key = Key::Backspace;
                    return ev;
                }
                ev.key.key = Key::Character;
                ev.key.ch = c;
                ev.key.text.assign(1, c);
                return ev;
            }

            if (raw.size() == 2 && raw[0] == '\x1B')
            {
                ev.type = EventType::Key;
                ev.key.key = Key::Character;
                ev.key.modifiers = KeyModifier::Alt;
                ev.key.ch = raw[1];
                ev.key.text.assign(1, raw[1]);
                return ev;
            }

            ev.type = EventType::Error;
            ev.error = "Unsupported input sequence";
            return ev;
        }

        bool TryPollStream(std::istream &in, int timeout_ms, PollResult *out)
        {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms < 0 ? 0 : timeout_ms);

            while (true)
            {
                if (in.rdbuf() != nullptr && in.rdbuf()->in_avail() > 0)
                {
                    int c = in.get();
                    if (c == EOF)
                    {
                        out->status = PollStatus::NoEvent;
                        return true;
                    }

                    std::string raw;
                    raw.push_back(static_cast<char>(c));
                    if (raw[0] == '\x1B' && in.rdbuf()->in_avail() > 0)
                    {
                        const int c1 = in.peek();
                        if (c1 == '[')
                        {
                            raw.push_back(static_cast<char>(in.get()));
                            for (int i = 0; i < 16; ++i)
                            {
                                if (in.rdbuf()->in_avail() <= 0)
                                {
                                    break;
                                }
                                const int cx = in.get();
                                if (cx == EOF)
                                {
                                    break;
                                }
                                const char ch = static_cast<char>(cx);
                                raw.push_back(ch);
                                if (IsAsciiLetter(ch) || ch == '~')
                                {
                                    break;
                                }
                            }
                        }
                        else if (c1 != EOF)
                        {
                            raw.push_back(static_cast<char>(in.get()));
                        }
                    }

                    out->event = ParseEscapedInput(raw);
                    out->status = (out->event.type == EventType::Error) ? PollStatus::Error : PollStatus::HasEvent;
                    out->message = out->event.error;
                    return true;
                }

                if (timeout_ms == 0)
                {
                    out->status = PollStatus::NoEvent;
                    return true;
                }

                if (std::chrono::steady_clock::now() >= deadline)
                {
                    out->status = PollStatus::NoEvent;
                    return true;
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }

#if defined(_WIN32)
        WORD MapFg(Color c)
        {
            const int v = static_cast<int>(c);
            if (v < 0)
            {
                return 0;
            }

            const int base = (v <= 7) ? v : (v - 8);
            WORD out = 0;
            if ((base & 0x1) != 0)
            {
                out |= FOREGROUND_RED;
            }
            if ((base & 0x2) != 0)
            {
                out |= FOREGROUND_GREEN;
            }
            if ((base & 0x4) != 0)
            {
                out |= FOREGROUND_BLUE;
            }
            if (v >= 8)
            {
                out |= FOREGROUND_INTENSITY;
            }
            return out;
        }

        WORD MapBg(Color c)
        {
            const int v = static_cast<int>(c);
            if (v < 0)
            {
                return 0;
            }

            const int base = (v <= 7) ? v : (v - 8);
            WORD out = 0;
            if ((base & 0x1) != 0)
            {
                out |= BACKGROUND_RED;
            }
            if ((base & 0x2) != 0)
            {
                out |= BACKGROUND_GREEN;
            }
            if ((base & 0x4) != 0)
            {
                out |= BACKGROUND_BLUE;
            }
            if (v >= 8)
            {
                out |= BACKGROUND_INTENSITY;
            }
            return out;
        }

        bool TryInitWin32Console(std::ostream &out,
                                 std::uintptr_t *handle_out,
                                 std::uint16_t *default_attr)
        {
            HANDLE h = INVALID_HANDLE_VALUE;
            if (&out == &std::cout)
            {
                h = GetStdHandle(STD_OUTPUT_HANDLE);
            }
            else if (&out == &std::cerr)
            {
                h = GetStdHandle(STD_ERROR_HANDLE);
            }

            if (h == nullptr || h == INVALID_HANDLE_VALUE)
            {
                return false;
            }

            DWORD mode = 0;
            if (!GetConsoleMode(h, &mode))
            {
                return false;
            }

            CONSOLE_SCREEN_BUFFER_INFO info{};
            if (!GetConsoleScreenBufferInfo(h, &info))
            {
                return false;
            }

            *handle_out = reinterpret_cast<std::uintptr_t>(h);
            *default_attr = static_cast<std::uint16_t>(info.wAttributes);
            return true;
        }

#endif

    } // namespace

    class StreamInputSource final : public InputSource
    {
    public:
        StreamInputSource(std::istream &in, const InputOptions &options)
            : in_(in), mode_(options.consume_mode)
        {
        }

        PollResult Poll(int timeout_ms) override
        {
            PollResult result;
            result.status = PollStatus::NoEvent;
            if (timeout_ms < 0)
            {
                result.status = PollStatus::Error;
                result.message = "timeout_ms must be >= 0";
                return result;
            }

            if (!TryPollStream(in_, timeout_ms, &result))
            {
                result.status = PollStatus::Error;
                result.message = "Failed to poll stream";
                return result;
            }

            if (mode_ == InputConsumeMode::PeekOnly && result.status == PollStatus::HasEvent)
            {
                const std::streampos pos = in_.tellg();
                if (pos != std::streampos(-1))
                {
                    const std::streamoff consumed = static_cast<std::streamoff>(result.event.raw.size());
                    if (pos >= consumed)
                    {
                        in_.clear();
                        in_.seekg(pos - consumed);
                    }
                    else
                    {
                        result.message = "PeekOnly degraded to consume because rewind is unavailable";
                    }
                }
                else
                {
                    result.message = "PeekOnly degraded to consume for non-seekable stream";
                }
            }

            if (mode_ == InputConsumeMode::TeeBack && result.status == PollStatus::HasEvent)
            {
                result.message = "TeeBack is experimental on stream input and currently degrades to exclusive consume";
            }
            return result;
        }

        bool SetConsumeMode(InputConsumeMode mode) override
        {
            mode_ = mode;
            return true;
        }

        InputConsumeMode consume_mode() const noexcept override
        {
            return mode_;
        }

        InputConsumeSupport QueryConsumeModeSupport(InputConsumeMode mode) const noexcept override
        {
            switch (mode)
            {
            case InputConsumeMode::ExclusiveConsume:
                return InputConsumeSupport::Native;
            case InputConsumeMode::PeekOnly:
                return CanRewind() ? InputConsumeSupport::Native : InputConsumeSupport::Degraded;
            case InputConsumeMode::TeeBack:
                return InputConsumeSupport::Degraded;
            }
            return InputConsumeSupport::Unsupported;
        }

    private:
        bool CanRewind() const noexcept
        {
            auto &stream = const_cast<std::istream &>(in_);
            return stream.good() && stream.tellg() != std::streampos(-1);
        }

        std::istream &in_;
        InputConsumeMode mode_{InputConsumeMode::ExclusiveConsume};
    };

#if defined(_WIN32)
    class ConsoleInputSource final : public InputSource
    {
    public:
        explicit ConsoleInputSource(const InputOptions &options)
            : mode_(options.consume_mode)
        {
            in_ = GetStdHandle(STD_INPUT_HANDLE);
            if (in_ == nullptr || in_ == INVALID_HANDLE_VALUE)
            {
                ok_ = false;
                return;
            }

            DWORD mode = 0;
            if (!GetConsoleMode(in_, &mode))
            {
                ok_ = false;
                return;
            }

            original_mode_ = mode;
            DWORD next_mode = mode;
            next_mode |= ENABLE_EXTENDED_FLAGS;
            next_mode &= ~ENABLE_QUICK_EDIT_MODE;
            if (options.enable_mouse)
            {
                next_mode |= ENABLE_MOUSE_INPUT;
            }
            if (options.enable_resize_events)
            {
                next_mode |= ENABLE_WINDOW_INPUT;
            }
            SetConsoleMode(in_, next_mode);
        }

        ~ConsoleInputSource() override
        {
            if (ok_)
            {
                SetConsoleMode(in_, original_mode_);
            }
        }

        PollResult Poll(int timeout_ms) override
        {
            PollResult result;
            if (!ok_)
            {
                result.status = PollStatus::Error;
                result.message = "Console input is unavailable";
                return result;
            }

            if (timeout_ms < 0)
            {
                result.status = PollStatus::Error;
                result.message = "timeout_ms must be >= 0";
                return result;
            }

            const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
            while (true)
            {
                INPUT_RECORD rec{};
                DWORD count = 0;
                if (!PeekConsoleInput(in_, &rec, 1, &count))
                {
                    result.status = PollStatus::Error;
                    result.message = "PeekConsoleInput failed";
                    return result;
                }

                if (count == 0)
                {
                    if (timeout_ms == 0 || std::chrono::steady_clock::now() >= deadline)
                    {
                        result.status = PollStatus::NoEvent;
                        return result;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    continue;
                }

                DWORD read = 0;
                if (mode_ == InputConsumeMode::PeekOnly)
                {
                    if (!PeekConsoleInput(in_, &rec, 1, &read))
                    {
                        result.status = PollStatus::Error;
                        result.message = "PeekConsoleInput failed";
                        return result;
                    }
                }
                else if (mode_ == InputConsumeMode::TeeBack)
                {
                    if (!ReadConsoleInput(in_, &rec, 1, &read))
                    {
                        result.status = PollStatus::Error;
                        result.message = "ReadConsoleInput failed";
                        return result;
                    }
                }
                else
                {
                    if (!ReadConsoleInput(in_, &rec, 1, &read))
                    {
                        result.status = PollStatus::Error;
                        result.message = "ReadConsoleInput failed";
                        return result;
                    }
                }

                if (read == 0)
                {
                    result.status = PollStatus::NoEvent;
                    return result;
                }

                if (rec.EventType == KEY_EVENT)
                {
                    const KEY_EVENT_RECORD &kr = rec.Event.KeyEvent;
                    if (!kr.bKeyDown)
                    {
                        if (mode_ == InputConsumeMode::PeekOnly)
                        {
                            result.status = PollStatus::NoEvent;
                            return result;
                        }
                        continue;
                    }

                    result.event.type = EventType::Key;
                    if ((kr.dwControlKeyState & SHIFT_PRESSED) != 0)
                    {
                        result.event.key.modifiers |= KeyModifier::Shift;
                    }
                    if ((kr.dwControlKeyState & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) != 0)
                    {
                        result.event.key.modifiers |= KeyModifier::Alt;
                    }
                    if ((kr.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0)
                    {
                        result.event.key.modifiers |= KeyModifier::Ctrl;
                    }

                    switch (kr.wVirtualKeyCode)
                    {
                    case VK_UP:
                        result.event.key.key = Key::ArrowUp;
                        break;
                    case VK_DOWN:
                        result.event.key.key = Key::ArrowDown;
                        break;
                    case VK_LEFT:
                        result.event.key.key = Key::ArrowLeft;
                        break;
                    case VK_RIGHT:
                        result.event.key.key = Key::ArrowRight;
                        break;
                    case VK_RETURN:
                        result.event.key.key = Key::Enter;
                        break;
                    case VK_ESCAPE:
                        result.event.key.key = Key::Escape;
                        break;
                    case VK_TAB:
                        result.event.key.key = Key::Tab;
                        break;
                    case VK_BACK:
                        result.event.key.key = Key::Backspace;
                        break;
                    case VK_DELETE:
                        result.event.key.key = Key::Delete;
                        break;
                    case VK_HOME:
                        result.event.key.key = Key::Home;
                        break;
                    case VK_END:
                        result.event.key.key = Key::End;
                        break;
                    case VK_PRIOR:
                        result.event.key.key = Key::PageUp;
                        break;
                    case VK_NEXT:
                        result.event.key.key = Key::PageDown;
                        break;
                    case VK_INSERT:
                        result.event.key.key = Key::Insert;
                        break;
                    default:
                        result.event.key.key = Key::Character;
                        break;
                    }

                    const unsigned char ch = static_cast<unsigned char>(kr.uChar.AsciiChar);
                    if (ch != 0)
                    {
                        result.event.key.ch = static_cast<char>(ch);
                        result.event.key.text.assign(1, result.event.key.ch);
                        result.event.raw.assign(1, result.event.key.ch);
                    }
                    if (mode_ == InputConsumeMode::TeeBack)
                    {
                        result.message = "TeeBack is experimental on console input and currently degrades to exclusive consume";
                    }
                    result.status = PollStatus::HasEvent;
                    return result;
                }

                if (rec.EventType == MOUSE_EVENT)
                {
                    const MOUSE_EVENT_RECORD &mr = rec.Event.MouseEvent;
                    result.event.type = EventType::Mouse;
                    result.event.mouse.x = static_cast<std::uint16_t>(mr.dwMousePosition.X);
                    result.event.mouse.y = static_cast<std::uint16_t>(mr.dwMousePosition.Y);

                    if ((mr.dwControlKeyState & SHIFT_PRESSED) != 0)
                    {
                        result.event.mouse.modifiers |= KeyModifier::Shift;
                    }
                    if ((mr.dwControlKeyState & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) != 0)
                    {
                        result.event.mouse.modifiers |= KeyModifier::Alt;
                    }
                    if ((mr.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0)
                    {
                        result.event.mouse.modifiers |= KeyModifier::Ctrl;
                    }

                    if (mr.dwEventFlags == MOUSE_MOVED)
                    {
                        result.event.mouse.action = MouseAction::Move;
                    }
                    else if (mr.dwEventFlags == DOUBLE_CLICK)
                    {
                        result.event.mouse.action = MouseAction::DoubleClick;
                    }
                    else if (mr.dwEventFlags == MOUSE_WHEELED)
                    {
                        result.event.mouse.action = MouseAction::Wheel;
                        const int delta = static_cast<short>(HIWORD(mr.dwButtonState));
                        result.event.mouse.wheel_delta = delta;
                    }
                    else
                    {
                        if ((mr.dwButtonState & FROM_LEFT_1ST_BUTTON_PRESSED) != 0)
                        {
                            result.event.mouse.button = MouseButton::Left;
                            result.event.mouse.action = MouseAction::ButtonDown;
                            last_left_down_ = true;
                            last_left_x_ = result.event.mouse.x;
                            last_left_y_ = result.event.mouse.y;
                        }
                        else if ((mr.dwButtonState & RIGHTMOST_BUTTON_PRESSED) != 0)
                        {
                            result.event.mouse.button = MouseButton::Right;
                            result.event.mouse.action = MouseAction::ButtonDown;
                        }
                        else if ((mr.dwButtonState & FROM_LEFT_2ND_BUTTON_PRESSED) != 0)
                        {
                            result.event.mouse.button = MouseButton::Middle;
                            result.event.mouse.action = MouseAction::ButtonDown;
                        }
                        else
                        {
                            result.event.mouse.button = MouseButton::Left;
                            result.event.mouse.action = MouseAction::ButtonUp;
                            if (last_left_down_ &&
                                last_left_x_ == result.event.mouse.x &&
                                last_left_y_ == result.event.mouse.y)
                            {
                                result.event.mouse.action = MouseAction::Click;
                            }
                            last_left_down_ = false;
                        }
                    }

                    result.status = PollStatus::HasEvent;
                    if (mode_ == InputConsumeMode::TeeBack)
                    {
                        result.message = "TeeBack is experimental on console input and currently degrades to exclusive consume";
                    }
                    return result;
                }

                if (rec.EventType == WINDOW_BUFFER_SIZE_EVENT)
                {
                    result.event.type = EventType::Resize;
                    result.event.resize.size.cols = static_cast<std::uint16_t>(rec.Event.WindowBufferSizeEvent.dwSize.X);
                    result.event.resize.size.rows = static_cast<std::uint16_t>(rec.Event.WindowBufferSizeEvent.dwSize.Y);
                    result.event.resize.size.buffer_cols = result.event.resize.size.cols;
                    result.event.resize.size.buffer_rows = result.event.resize.size.rows;
                    result.status = PollStatus::HasEvent;
                    if (mode_ == InputConsumeMode::TeeBack)
                    {
                        result.message = "TeeBack is experimental on console input and currently degrades to exclusive consume";
                    }
                    return result;
                }

                if (mode_ == InputConsumeMode::PeekOnly)
                {
                    result.status = PollStatus::NoEvent;
                    return result;
                }
            }
        }

        bool SetConsumeMode(InputConsumeMode mode) override
        {
            mode_ = mode;
            return true;
        }

        InputConsumeMode consume_mode() const noexcept override
        {
            return mode_;
        }

        InputConsumeSupport QueryConsumeModeSupport(InputConsumeMode mode) const noexcept override
        {
            switch (mode)
            {
            case InputConsumeMode::ExclusiveConsume:
            case InputConsumeMode::PeekOnly:
                return InputConsumeSupport::Native;
            case InputConsumeMode::TeeBack:
                return InputConsumeSupport::Degraded;
            }
            return InputConsumeSupport::Unsupported;
        }

    private:
        HANDLE in_{INVALID_HANDLE_VALUE};
        DWORD original_mode_{0};
        bool ok_{true};
        InputConsumeMode mode_{InputConsumeMode::ExclusiveConsume};
        bool last_left_down_{false};
        std::uint16_t last_left_x_{0};
        std::uint16_t last_left_y_{0};
    };
#endif

    FrameBuffer::FrameBuffer(std::uint16_t width, std::uint16_t height, char fill)
        : width_(width), height_(height), data_(static_cast<std::size_t>(width) * static_cast<std::size_t>(height))
    {
        Clear(fill);
    }

    std::uint16_t FrameBuffer::width() const noexcept
    {
        return width_;
    }

    std::uint16_t FrameBuffer::height() const noexcept
    {
        return height_;
    }

    void FrameBuffer::Clear(char fill)
    {
        for (FrameCell &cell : data_)
        {
            cell.utf8.assign(1, fill);
            cell.display_width = 1;
            cell.continuation = false;
        }
    }

    bool FrameBuffer::Put(std::uint16_t x, std::uint16_t y, std::string_view utf8, std::uint8_t display_width)
    {
        if (x >= width_ || y >= height_)
        {
            return false;
        }

        const auto clear_anchor = [this, y](std::uint16_t anchor_x)
        {
            if (anchor_x >= width_)
            {
                return;
            }
            const std::size_t anchor_idx = static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + anchor_x;
            const std::uint16_t span = std::max<std::uint16_t>(1, data_[anchor_idx].display_width);
            for (std::uint16_t d = 0; d < span; ++d)
            {
                const std::uint16_t tx = static_cast<std::uint16_t>(anchor_x + d);
                if (tx >= width_)
                {
                    break;
                }
                const std::size_t idx = static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + tx;
                data_[idx].utf8 = " ";
                data_[idx].display_width = 1;
                data_[idx].continuation = false;
            }
        };

        const std::uint16_t safe_width = std::max<std::uint16_t>(1, display_width == 0 ? EstimateDisplayWidth(utf8) : display_width);
        const std::uint16_t left_anchor = FindCellAnchorX(*this, x, y);
        clear_anchor(left_anchor);
        for (std::uint16_t d = 0; d < safe_width; ++d)
        {
            const std::uint16_t tx = static_cast<std::uint16_t>(x + d);
            if (tx >= width_)
            {
                break;
            }
            clear_anchor(FindCellAnchorX(*this, tx, y));
        }

        const std::size_t idx = static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + x;
        data_[idx].utf8.assign(utf8.data(), utf8.size());
        data_[idx].display_width = static_cast<std::uint8_t>(safe_width);
        data_[idx].continuation = false;
        for (std::uint16_t d = 1; d < safe_width; ++d)
        {
            const std::uint16_t tx = static_cast<std::uint16_t>(x + d);
            if (tx >= width_)
            {
                break;
            }
            const std::size_t tail_idx = static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + tx;
            data_[tail_idx].utf8 = " ";
            data_[tail_idx].display_width = 1;
            data_[tail_idx].continuation = true;
        }
        return true;
    }

    const FrameCell *FrameBuffer::Get(std::uint16_t x, std::uint16_t y) const
    {
        if (x >= width_ || y >= height_)
        {
            return nullptr;
        }
        const std::size_t idx = static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + x;
        return &data_[idx];
    }

    void Widget::Layout(const Rect &bounds)
    {
        bounds_ = bounds;
    }

    bool Widget::HitTest(std::uint16_t x, std::uint16_t y) const
    {
        return visible_ &&
               x >= bounds_.x &&
               y >= bounds_.y &&
               x < static_cast<std::uint16_t>(bounds_.x + bounds_.width) &&
               y < static_cast<std::uint16_t>(bounds_.y + bounds_.height);
    }

    bool Widget::HandleEvent(const InputEvent &)
    {
        return false;
    }

    void Widget::OnFocusChanged(bool)
    {
    }

    const Rect &Widget::bounds() const noexcept
    {
        return bounds_;
    }

    void Widget::SetVisible(bool visible) noexcept
    {
        visible_ = visible;
    }

    bool Widget::visible() const noexcept
    {
        return visible_;
    }

    void Widget::SetEnabled(bool enabled) noexcept
    {
        enabled_ = enabled;
    }

    bool Widget::enabled() const noexcept
    {
        return enabled_;
    }

    void Widget::SetFocusable(bool focusable) noexcept
    {
        focusable_ = focusable;
    }

    bool Widget::focusable() const noexcept
    {
        return focusable_;
    }

    void Widget::SetFocused(bool focused) noexcept
    {
        if (focused_ == focused)
        {
            return;
        }
        focused_ = focused;
        OnFocusChanged(focused_);
    }

    bool Widget::focused() const noexcept
    {
        return focused_;
    }

    void Layout::AddChild(std::shared_ptr<Widget> child)
    {
        if (child)
        {
            children_.push_back(std::move(child));
        }
    }

    const std::vector<std::shared_ptr<Widget>> &Layout::Children() const noexcept
    {
        return children_;
    }

    bool Layout::HandleEvent(const InputEvent &event)
    {
        if (event.type == EventType::Mouse)
        {
            for (auto it = children_.rbegin(); it != children_.rend(); ++it)
            {
                Widget *child = it->get();
                if (child != nullptr && child->visible() && child->enabled() &&
                    child->HitTest(event.mouse.x, event.mouse.y) &&
                    child->HandleEvent(event))
                {
                    return true;
                }
            }
        }

        if (event.type == EventType::Key)
        {
            for (const auto &child : children_)
            {
                if (child && child->visible() && child->enabled() && child->focused() && child->HandleEvent(event))
                {
                    return true;
                }
            }
        }

        for (const auto &child : children_)
        {
            if (child && child->visible() && child->enabled() && child->HandleEvent(event))
            {
                return true;
            }
        }
        return false;
    }

    void VerticalLayout::Layout(const Rect &bounds)
    {
        Widget::Layout(bounds);
        if (children_.empty())
        {
            return;
        }

        const std::uint16_t n = static_cast<std::uint16_t>(children_.size());
        const std::uint16_t base_h = (n == 0) ? 0 : static_cast<std::uint16_t>(bounds.height / n);
        const std::uint16_t extra = (n == 0) ? 0 : static_cast<std::uint16_t>(bounds.height % n);

        std::uint16_t y = bounds.y;
        for (std::uint16_t i = 0; i < n; ++i)
        {
            const std::uint16_t h = static_cast<std::uint16_t>(base_h + (i < extra ? 1 : 0));
            if (children_[i])
            {
                children_[i]->Layout(Rect{bounds.x, y, bounds.width, h});
            }
            y = static_cast<std::uint16_t>(y + h);
        }
    }

    void VerticalLayout::Render(FrameBuffer &frame) const
    {
        for (const auto &child : children_)
        {
            if (child && child->visible())
            {
                child->Render(frame);
            }
        }
    }

    void HorizontalLayout::Layout(const Rect &bounds)
    {
        Widget::Layout(bounds);
        if (children_.empty())
        {
            return;
        }

        const std::uint16_t n = static_cast<std::uint16_t>(children_.size());
        const std::uint16_t base_w = (n == 0) ? 0 : static_cast<std::uint16_t>(bounds.width / n);
        const std::uint16_t extra = (n == 0) ? 0 : static_cast<std::uint16_t>(bounds.width % n);

        std::uint16_t x = bounds.x;
        for (std::uint16_t i = 0; i < n; ++i)
        {
            const std::uint16_t w = static_cast<std::uint16_t>(base_w + (i < extra ? 1 : 0));
            if (children_[i])
            {
                children_[i]->Layout(Rect{x, bounds.y, w, bounds.height});
            }
            x = static_cast<std::uint16_t>(x + w);
        }
    }

    void HorizontalLayout::Render(FrameBuffer &frame) const
    {
        for (const auto &child : children_)
        {
            if (child && child->visible())
            {
                child->Render(frame);
            }
        }
    }

    Label::Label(std::string text)
        : text_(std::move(text))
    {
    }

    void Label::SetText(std::string text)
    {
        text_ = std::move(text);
    }

    const std::string &Label::text() const noexcept
    {
        return text_;
    }

    void Label::Render(FrameBuffer &frame) const
    {
        DrawUtf8Clipped(frame, bounds(), text_);
    }

    Button::Button(std::string text)
        : text_(std::move(text))
    {
        Widget::SetFocusable(true);
    }

    void Button::SetText(std::string text)
    {
        text_ = std::move(text);
    }

    const std::string &Button::text() const noexcept
    {
        return text_;
    }

    void Button::SetFocused(bool focused) noexcept
    {
        Widget::SetFocused(focused);
    }

    bool Button::focused() const noexcept
    {
        return Widget::focused();
    }

    void Button::SetOnClick(ClickHandler on_click)
    {
        on_click_ = std::move(on_click);
    }

    void Button::Render(FrameBuffer &frame) const
    {
        const std::string text = focused() ? ("> " + text_ + " <") : ("[ " + text_ + " ]");
        DrawUtf8Clipped(frame, bounds(), text);
    }

    bool Button::HandleEvent(const InputEvent &event)
    {
        if (!enabled() || !visible())
        {
            return false;
        }

        const auto fire_click = [this]() -> bool
        {
            if (on_click_)
            {
                on_click_();
            }
            return true;
        };

        if (event.type == EventType::Key && focused())
        {
            if (event.key.key == Key::Enter)
            {
                return fire_click();
            }
            if (event.key.key == Key::Character && event.key.ch == ' ')
            {
                return fire_click();
            }
        }

        if (event.type == EventType::Mouse &&
            event.mouse.button == MouseButton::Left &&
            (event.mouse.action == MouseAction::Click || event.mouse.action == MouseAction::ButtonDown) &&
            HitTest(event.mouse.x, event.mouse.y))
        {
            return fire_click();
        }
        return false;
    }

    Terminal::Terminal(std::ostream &out, bool ansi_enabled)
        : out_(&out), ansi_enabled_(ansi_enabled)
    {
#if defined(_WIN32)
        if (ansi_enabled_)
        {
            std::uintptr_t handle = 0;
            std::uint16_t attr = 0;
            if (TryInitWin32Console(out, &handle, &attr))
            {
                win32_enabled_ = true;
                win32_handle_ = handle;
                win32_default_attr_ = attr;
                ansi_enabled_ = false;
            }
        }
#endif
    }

    bool Terminal::ClearScreen()
    {
#if defined(_WIN32)
        if (win32_enabled_)
        {
            const HANDLE h = reinterpret_cast<HANDLE>(win32_handle_);
            CONSOLE_SCREEN_BUFFER_INFO info{};
            if (!GetConsoleScreenBufferInfo(h, &info))
            {
                return false;
            }

            const DWORD cells = static_cast<DWORD>(info.dwSize.X) * static_cast<DWORD>(info.dwSize.Y);
            DWORD written = 0;
            const COORD origin{0, 0};
            FillConsoleOutputCharacterA(h, ' ', cells, origin, &written);
            FillConsoleOutputAttribute(h, info.wAttributes, cells, origin, &written);
            return SetConsoleCursorPosition(h, origin) != 0;
        }
#endif
        if (!ansi_enabled_)
        {
            return false;
        }
        (*out_) << "\x1B[2J\x1B[H";
        return true;
    }

    bool Terminal::MoveTo(std::uint16_t x, std::uint16_t y)
    {
#if defined(_WIN32)
        if (win32_enabled_)
        {
            const HANDLE h = reinterpret_cast<HANDLE>(win32_handle_);
            const COORD pos{static_cast<SHORT>(x), static_cast<SHORT>(y)};
            return SetConsoleCursorPosition(h, pos) != 0;
        }
#endif
        if (!ansi_enabled_)
        {
            return false;
        }
        (*out_) << "\x1B[" << static_cast<unsigned>(y + 1) << ';' << static_cast<unsigned>(x + 1) << 'H';
        return true;
    }

    bool Terminal::SetCursorVisible(bool visible)
    {
#if defined(_WIN32)
        if (win32_enabled_)
        {
            const HANDLE h = reinterpret_cast<HANDLE>(win32_handle_);
            CONSOLE_CURSOR_INFO info{};
            if (GetConsoleCursorInfo(h, &info))
            {
                info.bVisible = visible ? TRUE : FALSE;
                return SetConsoleCursorInfo(h, &info) != 0;
            }
            return false;
        }
#endif
        if (!ansi_enabled_)
        {
            return false;
        }
        (*out_) << (visible ? "\x1B[?25h" : "\x1B[?25l");
        return true;
    }

    bool Terminal::SetCursorStyle(CursorStyle style)
    {
#if defined(_WIN32)
        if (win32_enabled_)
        {
            const HANDLE h = reinterpret_cast<HANDLE>(win32_handle_);
            CONSOLE_CURSOR_INFO info{};
            if (GetConsoleCursorInfo(h, &info))
            {
                info.bVisible = TRUE;
                switch (style)
                {
                case CursorStyle::Block:
                    info.dwSize = 100;
                    break;
                case CursorStyle::Underline:
                    info.dwSize = 20;
                    break;
                case CursorStyle::Bar:
                    info.dwSize = 60;
                    break;
                }
                return SetConsoleCursorInfo(h, &info) != 0;
            }
            return false;
        }
#endif
        if (!ansi_enabled_)
        {
            return false;
        }

        switch (style)
        {
        case CursorStyle::Block:
            (*out_) << "\x1B[2 q";
            break;
        case CursorStyle::Underline:
            (*out_) << "\x1B[4 q";
            break;
        case CursorStyle::Bar:
            (*out_) << "\x1B[6 q";
            break;
        }
        return true;
    }

    bool Terminal::SetColor(Color fg, Color bg)
    {
#if defined(_WIN32)
        if (win32_enabled_)
        {
            const HANDLE h = reinterpret_cast<HANDLE>(win32_handle_);
            WORD attr = static_cast<WORD>(win32_default_attr_);

            if (fg != Color::Default)
            {
                attr = static_cast<WORD>((attr & 0xFFF0u) | MapFg(fg));
            }
            if (bg != Color::Default)
            {
                attr = static_cast<WORD>((attr & 0xFF0Fu) | MapBg(bg));
            }

            return SetConsoleTextAttribute(h, attr) != 0;
        }
#endif
        if (!ansi_enabled_)
        {
            return false;
        }
        (*out_) << "\x1B[" << FgCode(fg) << ';' << BgCode(bg) << 'm';
        return true;
    }

    bool Terminal::ResetStyle()
    {
#if defined(_WIN32)
        if (win32_enabled_)
        {
            const HANDLE h = reinterpret_cast<HANDLE>(win32_handle_);
            return SetConsoleTextAttribute(h, static_cast<WORD>(win32_default_attr_)) != 0;
        }
#endif
        if (!ansi_enabled_)
        {
            return false;
        }
        (*out_) << "\x1B[0m";
        return true;
    }

    bool Terminal::Print(std::string_view text)
    {
        (*out_) << text;
        return true;
    }

    bool Terminal::ClearLine()
    {
#if defined(_WIN32)
        if (win32_enabled_)
        {
            const HANDLE h = reinterpret_cast<HANDLE>(win32_handle_);
            CONSOLE_SCREEN_BUFFER_INFO info{};
            if (!GetConsoleScreenBufferInfo(h, &info))
            {
                return false;
            }
            const SHORT line_left = info.srWindow.Left;
            const SHORT line_width = static_cast<SHORT>(info.srWindow.Right - info.srWindow.Left + 1);
            COORD start{line_left, info.dwCursorPosition.Y};
            DWORD written = 0;
            if (!FillConsoleOutputCharacterA(h, ' ', static_cast<DWORD>(line_width), start, &written))
            {
                return false;
            }
            if (!FillConsoleOutputAttribute(h, info.wAttributes, static_cast<DWORD>(line_width), start, &written))
            {
                return false;
            }
            return SetConsoleCursorPosition(h, info.dwCursorPosition) != 0;
        }
#endif
        if (!ansi_enabled_)
        {
            return false;
        }
        (*out_) << "\x1B[2K";
        return true;
    }

    bool Terminal::ClearToLineEnd()
    {
#if defined(_WIN32)
        if (win32_enabled_)
        {
            const HANDLE h = reinterpret_cast<HANDLE>(win32_handle_);
            CONSOLE_SCREEN_BUFFER_INFO info{};
            if (!GetConsoleScreenBufferInfo(h, &info))
            {
                return false;
            }

            const SHORT line_right = info.srWindow.Right;
            const SHORT cur_x = info.dwCursorPosition.X;
            if (cur_x > line_right)
            {
                return true;
            }
            const DWORD cells = static_cast<DWORD>(line_right - cur_x + 1);
            DWORD written = 0;
            if (!FillConsoleOutputCharacterA(h, ' ', cells, info.dwCursorPosition, &written))
            {
                return false;
            }
            if (!FillConsoleOutputAttribute(h, info.wAttributes, cells, info.dwCursorPosition, &written))
            {
                return false;
            }
            return SetConsoleCursorPosition(h, info.dwCursorPosition) != 0;
        }
#endif
        if (!ansi_enabled_)
        {
            return false;
        }
        (*out_) << "\x1B[K";
        return true;
    }

    bool Terminal::ClearRect(std::uint16_t x, std::uint16_t y, std::uint16_t width, std::uint16_t height)
    {
#if defined(_WIN32)
        if (win32_enabled_)
        {
            const HANDLE h = reinterpret_cast<HANDLE>(win32_handle_);
            CONSOLE_SCREEN_BUFFER_INFO info{};
            if (!GetConsoleScreenBufferInfo(h, &info))
            {
                return false;
            }

            const COORD original = info.dwCursorPosition;
            for (std::uint16_t row = 0; row < height; ++row)
            {
                COORD start{static_cast<SHORT>(x), static_cast<SHORT>(y + row)};
                DWORD written = 0;
                if (!FillConsoleOutputCharacterA(h, ' ', static_cast<DWORD>(width), start, &written))
                {
                    return false;
                }
                if (!FillConsoleOutputAttribute(h, info.wAttributes, static_cast<DWORD>(width), start, &written))
                {
                    return false;
                }
            }
            return SetConsoleCursorPosition(h, original) != 0;
        }
#endif
        for (std::uint16_t row = 0; row < height; ++row)
        {
            if (!MoveTo(x, static_cast<std::uint16_t>(y + row)))
            {
                return false;
            }
            if (!Print(std::string(width, ' ')))
            {
                return false;
            }
        }
        return true;
    }

    bool Terminal::GetCursorPosition(Position *out_pos) const
    {
        if (out_pos == nullptr)
        {
            return false;
        }
#if defined(_WIN32)
        if (win32_enabled_)
        {
            CONSOLE_SCREEN_BUFFER_INFO info{};
            const HANDLE h = reinterpret_cast<HANDLE>(win32_handle_);
            if (!GetConsoleScreenBufferInfo(h, &info))
            {
                return false;
            }
            out_pos->x = static_cast<std::uint16_t>(info.dwCursorPosition.X);
            out_pos->y = static_cast<std::uint16_t>(info.dwCursorPosition.Y);
            return true;
        }
#endif
        return false;
    }

    TerminalSize Terminal::GetSize() const
    {
        TerminalSize s;
#if defined(_WIN32)
        if (win32_enabled_)
        {
            CONSOLE_SCREEN_BUFFER_INFO info{};
            const HANDLE h = reinterpret_cast<HANDLE>(win32_handle_);
            if (GetConsoleScreenBufferInfo(h, &info))
            {
                s.cols = static_cast<std::uint16_t>(info.srWindow.Right - info.srWindow.Left + 1);
                s.rows = static_cast<std::uint16_t>(info.srWindow.Bottom - info.srWindow.Top + 1);
                s.buffer_cols = static_cast<std::uint16_t>(info.dwSize.X);
                s.buffer_rows = static_cast<std::uint16_t>(info.dwSize.Y);
            }
        }
#endif
        return s;
    }

    bool Terminal::SetTitle(std::string_view title)
    {
#if defined(_WIN32)
        if (win32_enabled_)
        {
            std::string tmp(title);
            return SetConsoleTitleA(tmp.c_str()) != 0;
        }
#endif
        if (!ansi_enabled_)
        {
            return false;
        }
        (*out_) << "\x1B]0;" << title << "\x07";
        return true;
    }

    std::size_t Terminal::RenderFrameDiff(const FrameBuffer &frame,
                                          FrameBuffer *previous,
                                          std::uint16_t origin_x,
                                          std::uint16_t origin_y)
    {
        std::size_t updates = 0;

        if (previous == nullptr || previous->width() != frame.width() || previous->height() != frame.height())
        {
            for (std::uint16_t y = 0; y < frame.height(); ++y)
            {
                MoveTo(origin_x, static_cast<std::uint16_t>(origin_y + y));
                for (std::uint16_t x = 0; x < frame.width();)
                {
                    const FrameCell *cell = frame.Get(x, y);
                    if (cell == nullptr)
                    {
                        ++x;
                        continue;
                    }
                    if (!cell->continuation)
                    {
                        Print(cell->utf8);
                        ++updates;
                    }
                    x = AdvanceDisplayCell(frame, x, y);
                }
            }
            return updates;
        }

        for (std::uint16_t y = 0; y < frame.height(); ++y)
        {
            for (std::uint16_t x = 0; x < frame.width();)
            {
                const FrameCell *now = frame.Get(x, y);
                const FrameCell *old = previous->Get(x, y);
                if (CellsEquivalent(now, old))
                {
                    x = AdvanceDisplayCell(frame, x, y);
                    continue;
                }

                if (now == nullptr)
                {
                    ++x;
                    continue;
                }

                if (now->continuation)
                {
                    ++x;
                    continue;
                }

                MoveTo(static_cast<std::uint16_t>(origin_x + x), static_cast<std::uint16_t>(origin_y + y));
                Print(now->utf8);
                ++updates;

                const std::uint16_t now_width = DisplayWidthOrOne(*now);
                const std::uint16_t old_width = (old != nullptr && !old->continuation) ? DisplayWidthOrOne(*old) : 1;
                if (old_width > now_width)
                {
                    const std::uint16_t tail = static_cast<std::uint16_t>(old_width - now_width);
                    Print(std::string(tail, ' '));
                    ++updates;
                }

                x = AdvanceDisplayCell(frame, x, y);
            }
        }

        *previous = frame;
        return updates;
    }

    bool Terminal::Flush()
    {
        out_->flush();
        return true;
    }

    Terminal::Backend Terminal::CurrentBackend() const noexcept
    {
        if (win32_enabled_)
        {
            return Backend::Win32;
        }
        if (ansi_enabled_)
        {
            return Backend::Ansi;
        }
        return Backend::None;
    }

    Application::Application(Terminal *terminal)
        : terminal_(terminal)
    {
    }

    void Application::SetRoot(std::shared_ptr<Widget> root)
    {
        if (focused_widget_ != nullptr)
        {
            focused_widget_->SetFocused(false);
            focused_widget_ = nullptr;
        }
        root_ = std::move(root);
        previous_frame_.reset();
        last_size_ = {};
        needs_repaint_ = true;
    }

    void Application::SetInputSource(std::unique_ptr<InputSource> input_source)
    {
        input_source_ = std::move(input_source);
    }

    bool Application::SetFocusedWidget(Widget *widget)
    {
        if (widget != nullptr)
        {
            if (!widget->focusable() || !widget->visible() || !widget->enabled())
            {
                return false;
            }
        }

        if (focused_widget_ == widget)
        {
            return true;
        }

        if (focused_widget_ != nullptr)
        {
            focused_widget_->SetFocused(false);
        }
        focused_widget_ = widget;
        if (focused_widget_ != nullptr)
        {
            focused_widget_->SetFocused(true);
        }
        needs_repaint_ = true;
        return true;
    }

    Widget *Application::FocusedWidget() const noexcept
    {
        return focused_widget_;
    }

    void Application::RequestRepaint() noexcept
    {
        needs_repaint_ = true;
    }

    bool Application::NeedsRepaint() const noexcept
    {
        return needs_repaint_;
    }

    std::size_t Application::render_count() const noexcept
    {
        return render_count_;
    }

    void Application::RequestExit() noexcept
    {
        running_ = false;
    }

    bool Application::Running() const noexcept
    {
        return running_;
    }

    bool Application::Tick(int timeout_ms)
    {
        if (!running_)
        {
            return false;
        }

        if (root_ && (focused_widget_ == nullptr || !focused_widget_->visible() || !focused_widget_->enabled()))
        {
            focused_widget_ = FindFocusedWidget(root_.get());
            if (focused_widget_ == nullptr)
            {
                std::vector<Widget *> focusables;
                CollectFocusableWidgets(root_.get(), &focusables);
                if (!focusables.empty())
                {
                    (void)SetFocusedWidget(focusables.front());
                }
            }
        }

        if (input_source_)
        {
            const PollResult polled = input_source_->Poll(timeout_ms);
            if (polled.status == PollStatus::Error)
            {
                return running_;
            }

            if (polled.status == PollStatus::HasEvent)
            {
                if (polled.event.type == EventType::Key && polled.event.key.key == Key::Escape)
                {
                    running_ = false;
                }
                else if (root_)
                {
                    bool handled = false;
                    if (polled.event.type == EventType::Key && polled.event.key.key == Key::Tab)
                    {
                        std::vector<Widget *> focusables;
                        CollectFocusableWidgets(root_.get(), &focusables);
                        if (!focusables.empty())
                        {
                            std::size_t index = 0;
                            for (std::size_t i = 0; i < focusables.size(); ++i)
                            {
                                if (focusables[i] == focused_widget_)
                                {
                                    index = i;
                                    break;
                                }
                            }
                            const bool reverse = HasModifier(polled.event.key.modifiers, KeyModifier::Shift);
                            if (reverse)
                            {
                                index = (index == 0) ? (focusables.size() - 1) : (index - 1);
                            }
                            else
                            {
                                index = (index + 1) % focusables.size();
                            }
                            handled = SetFocusedWidget(focusables[index]);
                        }
                    }
                    else if (polled.event.type == EventType::Key &&
                             (polled.event.key.key == Key::ArrowLeft ||
                              polled.event.key.key == Key::ArrowRight ||
                              polled.event.key.key == Key::ArrowUp ||
                              polled.event.key.key == Key::ArrowDown))
                    {
                        std::vector<Widget *> focusables;
                        CollectFocusableWidgets(root_.get(), &focusables);
                        if (focusables.size() > 1)
                        {
                            std::size_t index = 0;
                            for (std::size_t i = 0; i < focusables.size(); ++i)
                            {
                                if (focusables[i] == focused_widget_)
                                {
                                    index = i;
                                    break;
                                }
                            }
                            if (polled.event.key.key == Key::ArrowLeft || polled.event.key.key == Key::ArrowUp)
                            {
                                index = (index == 0) ? (focusables.size() - 1) : (index - 1);
                            }
                            else
                            {
                                index = (index + 1) % focusables.size();
                            }
                            handled = SetFocusedWidget(focusables[index]);
                        }
                    }
                    else if (polled.event.type == EventType::Mouse)
                    {
                        Widget *target = FindWidgetAt(root_.get(), polled.event.mouse.x, polled.event.mouse.y);
                        if (target != nullptr && target->focusable())
                        {
                            (void)SetFocusedWidget(target);
                        }
                        if (target != nullptr)
                        {
                            handled = target->HandleEvent(polled.event);
                        }
                    }

                    if (!handled && focused_widget_ != nullptr && polled.event.type == EventType::Key)
                    {
                        handled = focused_widget_->HandleEvent(polled.event);
                    }
                    if (!handled)
                    {
                        handled = root_->HandleEvent(polled.event);
                    }
                    (void)handled;
                    needs_repaint_ = true;
                }
            }
        }

        if (terminal_ != nullptr && root_)
        {
            TerminalSize size = terminal_->GetSize();
            const std::uint16_t width = (size.cols > 0) ? size.cols : 80;
            const std::uint16_t height = (size.rows > 0) ? size.rows : 25;
            const bool size_changed = size.cols != last_size_.cols ||
                                      size.rows != last_size_.rows ||
                                      size.buffer_cols != last_size_.buffer_cols ||
                                      size.buffer_rows != last_size_.buffer_rows;

            if (size_changed)
            {
                needs_repaint_ = true;
            }

            if (needs_repaint_)
            {
                FrameBuffer current(width, height, ' ');
                root_->Layout(Rect{0, 0, width, height});
                root_->Render(current);

                if (!previous_frame_ || previous_frame_->width() != width || previous_frame_->height() != height)
                {
                    previous_frame_ = std::make_unique<FrameBuffer>(width, height, ' ');
                }

                terminal_->RenderFrameDiff(current, previous_frame_.get(), 0, 0);
                terminal_->Flush();
                *previous_frame_ = current;
                last_size_ = size;
                needs_repaint_ = false;
                ++render_count_;
            }
        }

        return running_;
    }

    int Application::Run(int max_ticks, int timeout_ms)
    {
        int ticks = 0;
        while (running_)
        {
            Tick(timeout_ms);
            ++ticks;
            if (max_ticks >= 0 && ticks >= max_ticks)
            {
                break;
            }
        }
        return running_ ? 1 : 0;
    }

    std::unique_ptr<InputSource> CreateConsoleInputSource(const InputOptions &options)
    {
        (void)options;
#if defined(_WIN32)
        return std::make_unique<ConsoleInputSource>(options);
#else
        return nullptr;
#endif
    }

    std::unique_ptr<InputSource> CreateStreamInputSource(std::istream &in, const InputOptions &options)
    {
        return std::make_unique<StreamInputSource>(in, options);
    }

} // namespace tuix
