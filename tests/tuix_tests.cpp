#include <gtest/gtest.h>

#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "tuix.h"

namespace
{
    class MarkWidget final : public tuix::Widget
    {
    public:
        explicit MarkWidget(char marker, bool focusable = false)
            : marker_(marker)
        {
            SetFocusable(focusable);
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

        bool HandleEvent(const tuix::InputEvent &event) override
        {
            ++events_;
            last_type_ = event.type;
            return true;
        }

        int events() const noexcept
        {
            return events_;
        }

        tuix::EventType last_type() const noexcept
        {
            return last_type_;
        }

    private:
        char marker_;
        int events_{0};
        tuix::EventType last_type_{tuix::EventType::None};
    };

    class FakeInputSource final : public tuix::InputSource
    {
    public:
        explicit FakeInputSource(std::vector<tuix::PollResult> results)
            : results_(std::move(results))
        {
        }

        tuix::PollResult Poll(int) override
        {
            if (index_ >= results_.size())
            {
                return {};
            }
            return results_[index_++];
        }

        bool SetConsumeMode(tuix::InputConsumeMode mode) override
        {
            mode_ = mode;
            return true;
        }

        tuix::InputConsumeMode consume_mode() const noexcept override
        {
            return mode_;
        }

        tuix::InputConsumeSupport QueryConsumeModeSupport(tuix::InputConsumeMode mode) const noexcept override
        {
            return mode == tuix::InputConsumeMode::TeeBack ? tuix::InputConsumeSupport::Degraded
                                                           : tuix::InputConsumeSupport::Native;
        }

    private:
        std::vector<tuix::PollResult> results_;
        std::size_t index_{0};
        tuix::InputConsumeMode mode_{tuix::InputConsumeMode::ExclusiveConsume};
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
    EXPECT_EQ(p2.event.key.key, tuix::Key::ArrowUp);

    auto p3 = source->Poll(0);
    ASSERT_EQ(p3.status, tuix::PollStatus::HasEvent);
    EXPECT_EQ(p3.event.key.key, tuix::Key::Enter);
}

TEST(TuixInputTests, SupportsDetectableConsumeModeCapabilities)
{
    std::istringstream in(std::string("x"));
    auto source = tuix::CreateStreamInputSource(in);
    ASSERT_NE(source, nullptr);

    EXPECT_EQ(source->QueryConsumeModeSupport(tuix::InputConsumeMode::ExclusiveConsume),
              tuix::InputConsumeSupport::Native);
    EXPECT_EQ(source->QueryConsumeModeSupport(tuix::InputConsumeMode::PeekOnly),
              tuix::InputConsumeSupport::Native);
    EXPECT_EQ(source->QueryConsumeModeSupport(tuix::InputConsumeMode::TeeBack),
              tuix::InputConsumeSupport::Degraded);
}

TEST(TuixInputTests, PollStreamCsiModifierAndShiftTab)
{
    std::istringstream in(std::string("\x1B[1;5A\x1B[Z"));
    auto source = tuix::CreateStreamInputSource(in);
    ASSERT_NE(source, nullptr);

    auto p1 = source->Poll(0);
    ASSERT_EQ(p1.status, tuix::PollStatus::HasEvent);
    EXPECT_EQ(p1.event.key.key, tuix::Key::ArrowUp);
    EXPECT_TRUE(tuix::HasModifier(p1.event.key.modifiers, tuix::KeyModifier::Ctrl));

    auto p2 = source->Poll(0);
    ASSERT_EQ(p2.status, tuix::PollStatus::HasEvent);
    EXPECT_EQ(p2.event.key.key, tuix::Key::Tab);
    EXPECT_TRUE(tuix::HasModifier(p2.event.key.modifiers, tuix::KeyModifier::Shift));
}

TEST(TuixInputTests, StreamPeekOnlyDoesNotAdvanceSeekableStream)
{
    std::istringstream in(std::string("ab"));
    auto source = tuix::CreateStreamInputSource(in);
    ASSERT_NE(source, nullptr);
    ASSERT_TRUE(source->SetConsumeMode(tuix::InputConsumeMode::PeekOnly));

    auto p1 = source->Poll(0);
    auto p2 = source->Poll(0);
    ASSERT_EQ(p1.status, tuix::PollStatus::HasEvent);
    ASSERT_EQ(p2.status, tuix::PollStatus::HasEvent);
    EXPECT_EQ(p1.event.key.ch, 'a');
    EXPECT_EQ(p2.event.key.ch, 'a');
}

TEST(TuixInputTests, StreamTeeBackReturnsExplicitDegradeMessage)
{
    std::istringstream in(std::string("z"));
    auto source = tuix::CreateStreamInputSource(in);
    ASSERT_NE(source, nullptr);
    ASSERT_TRUE(source->SetConsumeMode(tuix::InputConsumeMode::TeeBack));

    auto p = source->Poll(0);
    ASSERT_EQ(p.status, tuix::PollStatus::HasEvent);
    EXPECT_FALSE(p.message.empty());
}

TEST(TuixFrameBufferTests, AutoDisplayWidthEstimationAndContinuationTracking)
{
    tuix::FrameBuffer fb(4, 1, ' ');
    ASSERT_TRUE(fb.Put(0, 0, "A", 0));
    ASSERT_TRUE(fb.Put(1, 0, "中", 0));

    const tuix::FrameCell *ascii = fb.Get(0, 0);
    const tuix::FrameCell *cjk = fb.Get(1, 0);
    const tuix::FrameCell *tail = fb.Get(2, 0);
    ASSERT_NE(ascii, nullptr);
    ASSERT_NE(cjk, nullptr);
    ASSERT_NE(tail, nullptr);
    EXPECT_EQ(ascii->display_width, 1u);
    EXPECT_EQ(cjk->display_width, 2u);
    EXPECT_TRUE(tail->continuation);
}

TEST(TuixFrameBufferTests, ReplacesWideCellWithoutLeavingDanglingTail)
{
    tuix::FrameBuffer fb(4, 1, ' ');
    ASSERT_TRUE(fb.Put(0, 0, "中", 0));
    ASSERT_TRUE(fb.Put(0, 0, "A", 1));

    const auto *c0 = fb.Get(0, 0);
    const auto *c1 = fb.Get(1, 0);
    ASSERT_NE(c0, nullptr);
    ASSERT_NE(c1, nullptr);
    EXPECT_EQ(c0->utf8, "A");
    EXPECT_FALSE(c0->continuation);
    EXPECT_EQ(c1->utf8, " ");
    EXPECT_FALSE(c1->continuation);
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
}

TEST(TuixFrameworkTests, LabelRendersUtf8WithDisplayWidthClipping)
{
    tuix::Label label("A中B");
    label.Layout(tuix::Rect{0, 0, 4, 1});

    tuix::FrameBuffer fb(4, 1, ' ');
    label.Render(fb);

    const auto *c0 = fb.Get(0, 0);
    const auto *c1 = fb.Get(1, 0);
    const auto *c2 = fb.Get(2, 0);
    const auto *c3 = fb.Get(3, 0);
    ASSERT_NE(c0, nullptr);
    ASSERT_NE(c1, nullptr);
    ASSERT_NE(c2, nullptr);
    ASSERT_NE(c3, nullptr);
    EXPECT_EQ(c0->utf8, "A");
    EXPECT_EQ(c1->utf8, "中");
    EXPECT_TRUE(c2->continuation);
    EXPECT_EQ(c3->utf8, "B");
}

TEST(TuixFrameworkTests, ButtonHandlesEnterSpaceAndMouseClick)
{
    int clicked = 0;
    tuix::Button button("go");
    button.Layout(tuix::Rect{0, 0, 8, 1});
    button.SetFocused(true);
    button.SetOnClick([&clicked]()
                      { ++clicked; });

    tuix::InputEvent enter;
    enter.type = tuix::EventType::Key;
    enter.key.key = tuix::Key::Enter;
    EXPECT_TRUE(button.HandleEvent(enter));

    tuix::InputEvent space;
    space.type = tuix::EventType::Key;
    space.key.key = tuix::Key::Character;
    space.key.ch = ' ';
    EXPECT_TRUE(button.HandleEvent(space));

    tuix::InputEvent click;
    click.type = tuix::EventType::Mouse;
    click.mouse.button = tuix::MouseButton::Left;
    click.mouse.action = tuix::MouseAction::Click;
    click.mouse.x = 1;
    click.mouse.y = 0;
    EXPECT_TRUE(button.HandleEvent(click));

    EXPECT_EQ(clicked, 3);
}

TEST(TuixFrameworkTests, LayoutRoutesMouseEventOnlyToHitTarget)
{
    auto left = std::make_shared<MarkWidget>('L');
    auto right = std::make_shared<MarkWidget>('R');

    tuix::HorizontalLayout layout;
    layout.AddChild(left);
    layout.AddChild(right);
    layout.Layout(tuix::Rect{0, 0, 8, 1});

    tuix::InputEvent click;
    click.type = tuix::EventType::Mouse;
    click.mouse.button = tuix::MouseButton::Left;
    click.mouse.action = tuix::MouseAction::Click;
    click.mouse.x = 6;
    click.mouse.y = 0;

    EXPECT_TRUE(layout.HandleEvent(click));
    EXPECT_EQ(left->events(), 0);
    EXPECT_EQ(right->events(), 1);
}

TEST(TuixFrameworkTests, ApplicationCyclesFocusWithTabAndShiftTab)
{
    auto root = std::make_shared<tuix::HorizontalLayout>();
    auto left = std::make_shared<tuix::Button>("left");
    auto right = std::make_shared<tuix::Button>("right");
    root->AddChild(left);
    root->AddChild(right);

    std::istringstream in(std::string("\t\x1B[Z\x1B"));
    auto source = tuix::CreateStreamInputSource(in);
    ASSERT_NE(source, nullptr);

    tuix::Application app(nullptr);
    app.SetRoot(root);
    app.SetInputSource(std::move(source));

    EXPECT_TRUE(app.Tick(0));
    EXPECT_EQ(app.FocusedWidget(), right.get());
    EXPECT_TRUE(app.Tick(0));
    EXPECT_EQ(app.FocusedWidget(), left.get());
    EXPECT_FALSE(app.Tick(0));
}

TEST(TuixFrameworkTests, ApplicationDispatchesFocusedKeyEvent)
{
    std::istringstream in(std::string(" \x1B"));
    auto source = tuix::CreateStreamInputSource(in);
    ASSERT_NE(source, nullptr);

    int clicked = 0;
    auto button = std::make_shared<tuix::Button>("go");
    button->SetOnClick([&clicked]()
                       { ++clicked; });

    tuix::Application app(nullptr);
    app.SetRoot(button);
    app.SetInputSource(std::move(source));

    EXPECT_TRUE(app.Tick(0));
    EXPECT_EQ(clicked, 1);
    EXPECT_FALSE(app.Tick(0));
}

TEST(TuixFrameworkTests, ApplicationOnlyRepaintsWhenDirty)
{
    std::ostringstream out;
    tuix::Terminal terminal(out, true);
    auto label = std::make_shared<tuix::Label>("steady");

    std::istringstream in{std::string()};
    auto source = tuix::CreateStreamInputSource(in);
    ASSERT_NE(source, nullptr);

    tuix::Application app(&terminal);
    app.SetRoot(label);
    app.SetInputSource(std::move(source));

    EXPECT_TRUE(app.Tick(0));
    EXPECT_EQ(app.render_count(), 1u);
    const std::string first = out.str();

    EXPECT_TRUE(app.Tick(0));
    EXPECT_EQ(app.render_count(), 1u);
    EXPECT_EQ(out.str(), first);

    app.RequestRepaint();
    EXPECT_TRUE(app.Tick(0));
    EXPECT_EQ(app.render_count(), 2u);
}

TEST(TuixFrameworkTests, ResizeEventTriggersRepaint)
{
    std::ostringstream out;
    tuix::Terminal terminal(out, true);
    auto label = std::make_shared<tuix::Label>("resize");

    tuix::PollResult resize;
    resize.status = tuix::PollStatus::HasEvent;
    resize.event.type = tuix::EventType::Resize;
    resize.event.resize.size.cols = 100;
    resize.event.resize.size.rows = 30;
    resize.event.resize.size.buffer_cols = 100;
    resize.event.resize.size.buffer_rows = 30;

    auto source = std::make_unique<FakeInputSource>(std::vector<tuix::PollResult>{resize});

    tuix::Application app(&terminal);
    app.SetRoot(label);
    app.SetInputSource(std::move(source));

    EXPECT_TRUE(app.Tick(0));
    EXPECT_EQ(app.render_count(), 1u);
}

#if defined(_WIN32)
TEST(TuixTerminalTests, PrefersWin32BackendWhenConsoleIsAvailable)
{
    tuix::Terminal t(std::cout, true);
    const auto backend = t.CurrentBackend();
    EXPECT_TRUE(backend == tuix::Terminal::Backend::Win32 || backend == tuix::Terminal::Backend::Ansi);
}
#endif
