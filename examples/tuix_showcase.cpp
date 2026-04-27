#include <iostream>
#include <memory>
#include <sstream>
#include <string>

#include "tuix.h"

namespace
{
    std::string backend_name(tuix::Terminal::Backend backend)
    {
        switch (backend)
        {
        case tuix::Terminal::Backend::Win32:
            return "win32";
        case tuix::Terminal::Backend::Ansi:
            return "ansi";
        case tuix::Terminal::Backend::None:
            return "none";
        }
        return "unknown";
    }

    void set_text_if_changed(const std::shared_ptr<tuix::Label> &label,
                             std::string text,
                             tuix::Application &app)
    {
        if (label && label->text() != text)
        {
            label->SetText(std::move(text));
            app.RequestRepaint();
        }
    }
}

int main()
{
    system("chcp 65001 >nul");

    tuix::Terminal term(std::cout, true);
    term.ClearScreen();
    term.SetCursorVisible(false);
    term.SetTitle("tuix M4 showcase");

    tuix::Application app(&term);

    auto root = std::make_shared<tuix::VerticalLayout>();
    auto title = std::make_shared<tuix::Label>("tuix M4 showcase");
    auto subtitle = std::make_shared<tuix::Label>("Focus/Mouse/Resize/UTF-8 | 宽字符演示: A中B");
    auto backend = std::make_shared<tuix::Label>("backend=" + backend_name(term.CurrentBackend()));
    auto status = std::make_shared<tuix::Label>("status=Ready");
    auto focus = std::make_shared<tuix::Label>("focus=(none)");
    auto help = std::make_shared<tuix::Label>("Tab/Shift+Tab or arrows move focus | Enter/Space/click activate | Esc exits");

    auto row = std::make_shared<tuix::HorizontalLayout>();
    auto run_button = std::make_shared<tuix::Button>("Run");
    auto reset_button = std::make_shared<tuix::Button>("Reset");
    auto exit_button = std::make_shared<tuix::Button>("Exit");
    row->AddChild(run_button);
    row->AddChild(reset_button);
    row->AddChild(exit_button);

    root->AddChild(title);
    root->AddChild(subtitle);
    root->AddChild(backend);
    root->AddChild(status);
    root->AddChild(focus);
    root->AddChild(row);
    root->AddChild(help);

    struct DemoState
    {
        int run_count{0};
        std::string status_text{"Ready"};
        bool scripted_input{false};
    } state;

    run_button->SetOnClick([&]()
                           {
                               ++state.run_count;
                               state.status_text = "Run clicked count=" + std::to_string(state.run_count);
                               app.RequestRepaint();
                           });
    reset_button->SetOnClick([&]()
                             {
                                 state.run_count = 0;
                                 state.status_text = "State reset";
                                 app.RequestRepaint();
                             });
    exit_button->SetOnClick([&]()
                            {
                                state.status_text = "Exit requested";
                                app.RequestExit();
                            });

    tuix::InputOptions options;
    options.consume_mode = tuix::InputConsumeMode::ExclusiveConsume;
    options.enable_mouse = true;
    options.enable_resize_events = true;

    std::istringstream scripted("\t \t \t\n");
    std::unique_ptr<tuix::InputSource> input = tuix::CreateConsoleInputSource(options);
    if (!input)
    {
        state.scripted_input = true;
        input = tuix::CreateStreamInputSource(scripted, options);
        state.status_text = "Console input unavailable, scripted input fallback";
    }

    app.SetRoot(root);
    app.SetInputSource(std::move(input));
    app.RequestRepaint();

    while (app.Running())
    {
        tuix::Widget *focused = app.FocusedWidget();
        std::string focus_text = "focus=(none)";
        if (focused == run_button.get())
        {
            focus_text = "focus=Run";
        }
        else if (focused == reset_button.get())
        {
            focus_text = "focus=Reset";
        }
        else if (focused == exit_button.get())
        {
            focus_text = "focus=Exit";
        }

        set_text_if_changed(status, "status=" + state.status_text, app);
        set_text_if_changed(focus, focus_text, app);
        set_text_if_changed(backend, "backend=" + backend_name(term.CurrentBackend()) +
                                         " | renders=" + std::to_string(app.render_count()),
                            app);

        if (!app.Tick(16))
        {
            break;
        }
    }

    term.ResetStyle();
    term.SetCursorVisible(true);
    term.MoveTo(0, static_cast<std::uint16_t>(term.GetSize().rows > 0 ? term.GetSize().rows - 1 : 0));
    term.Print("\n");
    term.Flush();
    return 0;
}
