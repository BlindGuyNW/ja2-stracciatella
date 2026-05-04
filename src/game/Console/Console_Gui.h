#ifndef GAME_CONSOLE_GUI_H_
#define GAME_CONSOLE_GUI_H_

#include <string>
#include <vector>

/* GUI introspection: enumerate visible engine buttons and synthesize
 * clicks. Lets the screen-reader user drive most laptop/options chrome
 * without bespoke commands per screen. */

void Cmd_Gui(const std::vector<std::string>& args);

#endif // GAME_CONSOLE_GUI_H_
