#include <iostream>
#include <sstream>

#include "tuix.h"

int main()
{
    tuix::Terminal term(std::cout, true);
    const auto backend = term.CurrentBackend();

    term.ClearScreen();
    term.SetCursorVisible(false);
    term.MoveTo(0, 0);
    term.SetColor(tuix::Color::BrightCyan, tuix::Color::Default);
    term.Print("tuix demo\n");
    term.ResetStyle();

    if (backend == tuix::Terminal::Backend::Win32)
    {
        term.Print("backend=win32\n");
    }
    else if (backend == tuix::Terminal::Backend::Ansi)
    {
        term.Print("backend=ansi\n");
    }
    else
    {
        term.Print("backend=none\n");
    }

    tuix::FrameBuffer prev(12, 2, ' ');
    tuix::FrameBuffer curr(12, 2, ' ');
    curr.Put(0, 0, "H", 1);
    curr.Put(1, 0, "i", 1);
    curr.Put(0, 1, "V", 1);
    curr.Put(1, 1, "2", 1);
    term.RenderFrameDiff(curr, &prev, 0, 2);

    // Demo poll-based input flow for non-interactive environments.
    std::istringstream demo_input("w\x1B[A\n");
    auto input = tuix::CreateStreamInputSource(demo_input);
    while (true)
    {
        const auto polled = input->Poll(0);
        if (polled.status != tuix::PollStatus::HasEvent)
        {
            break;
        }
        term.Print("event=");
        term.Print(polled.event.raw);
        term.Print("\n");
    }

    term.SetCursorVisible(true);
    term.Flush();
    return 0;
}
