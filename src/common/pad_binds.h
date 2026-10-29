#pragma once
#include <cstddef>
#include <string>

#include "common/mouse_keyboard.h"

// a controller's buttons, hats and axes onto the Xbox pad's, for the CONTROLLER page (native/input.cpp). An action
// with no input of its own keeps SDL's mapping; an input bound to an action no longer presses its mapped one.
namespace input {

const Action* PadActions(size_t* count);  // `defaults` names the mapping's input
std::string PadBinding(size_t action);     // the bound input's name, empty for the mapping's
void PadBind(size_t action, const std::string& source);  // empty goes back to the mapping
void ResetPadBindings();
void LoadPadBindings();      // after the settings load
std::string PressedPadInput();  // an input of any controller pressed now, empty for none

// the buttons the settings panel steps with, of a controller Android handed over without its device (see Loose in
// native/input.cpp): SDL has no gamepad for it, so the panel reads it here
struct MenuButtons {
    bool up = false, down = false, left = false, right = false;
    bool a = false, b = false, x = false, y = false;
    bool l1 = false, r1 = false, start = false, back = false;
};
bool LooseMenuButtons(MenuButtons& out);  // false without such a controller

}  // namespace input
