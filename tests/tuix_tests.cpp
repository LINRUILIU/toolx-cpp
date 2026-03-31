#include <gtest/gtest.h>

#include <iostream>
#include <sstream>
#include <string>
#include <memory>

#include "tuix.h"

namespace
{
    class MarkWidget final : public tuix::Widget
    {
    public:
        explicit MarkWidget(char marker)
            : marker_(marker)
        {
        }

        void Render(tuix::FrameBuffer &frame) const override
        {
            const auto b = bounds();
            if (b.width == 0 || b.height == 0)
            {
                return;
            }
            frame.Put(b.x, b.y, std::string(1, marker_), 1);
        }

        bool HandleEvent(const tuix::InputEvent &) override
        {
            ++events_;
            return true;
        }

        int events() const noexcept
        {
            return events_;
        }

    private:
        char marker_;
        int events_{0};
    };
}

TEST(TuixTerminalTests, EmitsAnsiSequences)
{
    std::ostringstream out;
    tuix::Terminal t(out, true);

    EXPECT_EQ(t.CurrentBackend(), tuix::Terminal::Backend::Ansi);

    EXPECT_TRUE(t.ClearScreen());
    EXPECT_TRUE(t.MoveTo(2, 1));
    EXPECT_TRUE(t.SetCursorVisible(false));
    EXPECT_TRUE(t.SetColor(tuix::Color::BrightGreen, tuix::Color::Black));
    EXPECT_TRUE(t.Print("ok"));
    EXPECT_TRUE(t.ResetStyle());
    EXPECT_TRUE(t.SetCursorVisible(true));

    const std::string s = out.str();
    EXPECT_NE(s.find("\x1B[2J\x1B[H"), std::string::npos);
    EXPECT_NE(s.find("\x1B[2;3H"), std::string::npos);
    EXPECT_NE(s.find("\x1B[?25l"), std::string::npos);
    EXPECT_NE(s.find("\x1B[92;40m"), std::string::npos);
    EXPECT_NE(s.find("ok"), std::string::npos);
    EXPECT_NE(s.find("\x1B[0m"), std::string::npos);
    EXPECT_NE(s.find("\x1B[?25h"), std::string::npos);
}

TEST(TuixTerminalTests, NoAnsiWhenDisabled)
{
    std::ostringstream out;
    tuix::Terminal t(out, false);

    EXPECT_EQ(t.CurrentBackend(), tuix::Terminal::Backend::None);

    EXPECT_FALSE(t.ClearScreen());
    EXPECT_FALSE(t.MoveTo(9, 9));
    EXPECT_FALSE(t.SetCursorVisible(false));
    EXPECT_FALSE(t.SetColor(tuix::Color::Red, tuix::Color::Black));
    EXPECT_TRUE(t.Print("x"));
    EXPECT_FALSE(t.ResetStyle());
    EXPECT_FALSE(t.SetCursorVisible(true));

    EXPECT_EQ(out.str(), "x");
}

TEST(TuixInputTests, PollStreamInput)
{
    std::istringstream in(std::string("a\x1B[A\n"));
    auto source = tuix::CreateStreamInputSource(in);
    ASSERT_NE(source, nullptr);

    auto p1 = source->Poll(0);
    ASSERT_EQ(p1.status, tuix::PollStatus::HasEvent);
    ASSERT_EQ(p1.event.type, tuix::EventType::Key);
    EXPECT_EQ(p1.event.key.key, tuix::Key::Character);
    EXPECT_EQ(p1.event.key.ch, 'a');

    auto p2 = source->Poll(0);
    ASSERT_EQ(p2.status, tuix::PollStatus::HasEvent);
    ASSERT_EQ(p2.event.type, tuix::EventType::Key);
    EXPECT_EQ(p2.event.key.key, tuix::Key::ArrowUp);

    auto p3 = source->Poll(0);
    ASSERT_EQ(p3.status, tuix::PollStatus::HasEvent);
    ASSERT_EQ(p3.event.type, tuix::EventType::Key);
    EXPECT_EQ(p3.event.key.key, tuix::Key::Enter);

    auto p4 = source->Poll(0);
    EXPECT_EQ(p4.status, tuix::PollStatus::NoEvent);
}

TEST(TuixInputTests, PollStreamCsiModifier)
{
    std::istringstream in(std::string("\x1B[1;5A"));
    auto source = tuix::CreateStreamInputSource(in);
    ASSERT_NE(source, nullptr);

    auto p = source->Poll(0);
    ASSERT_EQ(p.status, tuix::PollStatus::HasEvent);
    ASSERT_EQ(p.event.type, tuix::EventType::Key);
    EXPECT_EQ(p.event.key.key, tuix::Key::ArrowUp);
    EXPECT_TRUE(tuix::HasModifier(p.event.key.modifiers, tuix::KeyModifier::Ctrl));
}

TEST(TuixInputTests, StreamInputConsumeModeAndTimeoutValidation)
{
    std::istringstream in(std::string("x"));
    auto source = tuix::CreateStreamInputSource(in);
    ASSERT_NE(source, nullptr);

    EXPECT_EQ(source->consume_mode(), tuix::InputConsumeMode::ExclusiveConsume);
    EXPECT_TRUE(source->SetConsumeMode(tuix::InputConsumeMode::PeekOnly));
    EXPECT_EQ(source->consume_mode(), tuix::InputConsumeMode::PeekOnly);
    EXPECT_TRUE(source->SetConsumeMode(tuix::InputConsumeMode::TeeBack));
    EXPECT_EQ(source->consume_mode(), tuix::InputConsumeMode::TeeBack);

    auto p = source->Poll(-1);
    EXPECT_EQ(p.status, tuix::PollStatus::Error);
}

TEST(TuixInputTests, StreamPeekOnlyDoesNotAdvanceSeekableStream)
{
    std::istringstream in(std::string("ab"));
    auto source = tuix::CreateStreamInputSource(in);
    ASSERT_NE(source, nullptr);
    ASSERT_TRUE(source->SetConsumeMode(tuix::InputConsumeMode::PeekOnly));

    auto p1 = source->Poll(0);
    ASSERT_EQ(p1.status, tuix::PollStatus::HasEvent);
    EXPECT_EQ(p1.event.key.key, tuix::Key::Character);
    EXPECT_EQ(p1.event.key.ch, 'a');

    auto p2 = source->Poll(0);
    ASSERT_EQ(p2.status, tuix::PollStatus::HasEvent);
    EXPECT_EQ(p2.event.key.key, tuix::Key::Character);
    EXPECT_EQ(p2.event.key.ch, 'a');
}

TEST(TuixInputTests, StreamTeeBackReturnsDegradeMessage)
{
    std::istringstream in(std::string("z"));
    auto source = tuix::CreateStreamInputSource(in);
    ASSERT_NE(source, nullptr);
    ASSERT_TRUE(source->SetConsumeMode(tuix::InputConsumeMode::TeeBack));

    auto p = source->Poll(0);
    ASSERT_EQ(p.status, tuix::PollStatus::HasEvent);
    EXPECT_EQ(p.event.key.key, tuix::Key::Character);
    EXPECT_FALSE(p.message.empty());
}

TEST(TuixFrameBufferTests, AutoDisplayWidthEstimation)
{
    tuix::FrameBuffer fb(4, 1, ' ');
    ASSERT_TRUE(fb.Put(0, 0, "A", 0));
    ASSERT_TRUE(fb.Put(1, 0, "中", 0));

    const tuix::FrameCell *ascii = fb.Get(0, 0);
    const tuix::FrameCell *cjk = fb.Get(1, 0);
    ASSERT_NE(ascii, nullptr);
    ASSERT_NE(cjk, nullptr);
    EXPECT_EQ(ascii->display_width, 1u);
    EXPECT_EQ(cjk->display_width, 2u);
}

TEST(TuixTerminalTests, RenderFrameDiffUpdatesOnlyChangedCells)
{
    std::ostringstream out;
    tuix::Terminal t(out, true);

    tuix::FrameBuffer oldf(3, 2, '.');
    tuix::FrameBuffer newf(3, 2, '.');
    newf.Put(1, 0, "X", 1);
    newf.Put(2, 1, "Y", 1);

    const std::size_t updates = t.RenderFrameDiff(newf, &oldf, 0, 0);
    EXPECT_EQ(updates, 2u);

    const std::string s = out.str();
    EXPECT_NE(s.find("\x1B[1;2H"), std::string::npos);
    EXPECT_NE(s.find("\x1B[2;3H"), std::string::npos);
}

TEST(TuixTerminalTests, RenderFrameDiffClearsTailWhenWidthShrinks)
{
    std::ostringstream out;
    tuix::Terminal t(out, true);

    tuix::FrameBuffer oldf(4, 1, ' ');
    tuix::FrameBuffer newf(4, 1, ' ');
    oldf.Put(0, 0, "中", 2);
    newf.Put(0, 0, "A", 1);

    const std::size_t updates = t.RenderFrameDiff(newf, &oldf, 0, 0);
    EXPECT_GE(updates, 2u);

    const std::string s = out.str();
    EXPECT_NE(s.find("\x1B[1;1H"), std::string::npos);
    EXPECT_NE(s.find("A "), std::string::npos);
}

TEST(TuixFrameworkTests, VerticalLayoutSplitsBoundsForChildren)
{
    auto c1 = std::make_shared<MarkWidget>('A');
    auto c2 = std::make_shared<MarkWidget>('B');

    tuix::VerticalLayout layout;
    layout.AddChild(c1);
    layout.AddChild(c2);
    layout.Layout(tuix::Rect{0, 0, 6, 5});

    EXPECT_EQ(c1->bounds().y, 0u);
    EXPECT_EQ(c1->bounds().height, 3u);
    EXPECT_EQ(c2->bounds().y, 3u);
    EXPECT_EQ(c2->bounds().height, 2u);

    tuix::FrameBuffer fb(6, 5, ' ');
    layout.Render(fb);

    const auto *top = fb.Get(0, 0);
    const auto *bottom = fb.Get(0, 3);
    ASSERT_NE(top, nullptr);
    ASSERT_NE(bottom, nullptr);
    EXPECT_EQ(top->utf8, "A");
    EXPECT_EQ(bottom->utf8, "B");
}

TEST(TuixFrameworkTests, LabelRendersTextWithClipping)
{
    tuix::Label label("hello");
    label.Layout(tuix::Rect{0, 0, 3, 1});

    tuix::FrameBuffer fb(3, 1, ' ');
    label.Render(fb);

    const auto *c0 = fb.Get(0, 0);
    const auto *c1 = fb.Get(1, 0);
    const auto *c2 = fb.Get(2, 0);
    ASSERT_NE(c0, nullptr);
    ASSERT_NE(c1, nullptr);
    ASSERT_NE(c2, nullptr);
    EXPECT_EQ(c0->utf8, "h");
    EXPECT_EQ(c1->utf8, "e");
    EXPECT_EQ(c2->utf8, "l");
}

TEST(TuixFrameworkTests, ButtonClickFiresOnEnterWhenFocused)
{
    int clicked = 0;
    auto button = std::make_shared<tuix::Button>("go");
    button->SetFocused(true);
    button->SetOnClick([&clicked]()
                       { ++clicked; });

    std::istringstream in(std::string("\n\x1B"));
    auto source = tuix::CreateStreamInputSource(in);
    ASSERT_NE(source, nullptr);

    tuix::Application app(nullptr);
    app.SetRoot(button);
    app.SetInputSource(std::move(source));

    EXPECT_TRUE(app.Tick(0));
    EXPECT_FALSE(app.Tick(0));
    EXPECT_EQ(clicked, 1);
}

TEST(TuixFrameworkTests, ApplicationDispatchesEventsAndStopsOnEscape)
{
    std::istringstream in(std::string("x\x1B"));
    auto source = tuix::CreateStreamInputSource(in);
    ASSERT_NE(source, nullptr);

    auto widget = std::make_shared<MarkWidget>('W');

    tuix::Application app(nullptr);
    app.SetRoot(widget);
    app.SetInputSource(std::move(source));

    EXPECT_TRUE(app.Running());
    EXPECT_TRUE(app.Tick(0));
    EXPECT_FALSE(app.Tick(0));
    EXPECT_FALSE(app.Running());
    EXPECT_GE(widget->events(), 2);
}

#if defined(_WIN32)
TEST(TuixTerminalTests, PrefersWin32BackendWhenConsoleIsAvailable)
{
    tuix::Terminal t(std::cout, true);
    const auto backend = t.CurrentBackend();
    EXPECT_TRUE(backend == tuix::Terminal::Backend::Win32 || backend == tuix::Terminal::Backend::Ansi);
}
#endif
