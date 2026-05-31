#include "tuix.h"

#include <iostream>
#include <memory>
#include <sstream>

int main()
{
    // Scenario 1: styled frame cells carry text plus color/bold metadata.
    tuix::FrameBuffer frame(20, 4, ' ');
    frame.PutStyled(0, 0, "T", tuix::CellStyle{tuix::Color::BrightCyan, tuix::Color::Default, true});
    std::cout << "cell-bold=" << frame.Get(0, 0)->bold << "\n";

    // Scenario 2: layouts expose gap, padding, and flex weights.
    auto label = std::make_shared<tuix::Label>("Name");
    auto input = std::make_shared<tuix::TextInput>("toolx");
    auto row = std::make_shared<tuix::HorizontalLayout>();
    row->SetPadding({1, 1, 1, 1});
    row->SetGap(1);
    row->SetFlexWeights({1, 3});
    row->AddChild(label);
    row->AddChild(input);
    row->Layout({0, 0, 20, 3});

    // Scenario 3: Panel is a framed container and can host existing widgets.
    tuix::Panel panel("config");
    panel.AddChild(row);
    panel.Layout({0, 0, 22, 5});
    panel.Render(frame);

    // Scenario 4: TextInput and ListView handle direct events for scripted tests.
    input->SetFocused(true);
    tuix::InputEvent typed;
    typed.type = tuix::EventType::Key;
    typed.key.key = tuix::Key::Character;
    typed.key.ch = '!';
    typed.key.text = "!";
    input->HandleEvent(typed);

    tuix::ListView list({"one", "two", "three"});
    list.Layout({0, 0, 10, 3});
    list.SetFocused(true);
    tuix::InputEvent down;
    down.type = tuix::EventType::Key;
    down.key.key = tuix::Key::ArrowDown;
    list.HandleEvent(down);
    std::cout << "input=" << input->text() << " selected=" << list.selected_index() << "\n";

    // Scenario 5: terminal diff rendering can target any ostream.
    std::ostringstream out;
    tuix::Terminal terminal(out, true);
    terminal.RenderFrameDiff(frame, nullptr);
    std::cout << "ansi-bytes=" << out.str().size() << "\n";
    return 0;
}
