#ifndef GAME_CONSOLE_ROWS_H_
#define GAME_CONSOLE_ROWS_H_

#include <string>
#include <vector>

/* Text-row introspection: enumerate text drawn through MPrint this frame
 * and synthesize a click on the underlying mouse region. Bridges the gap
 * for screens whose interactive rows aren't GUI_BUTTONs (laptop list
 * rows, save-game slots) and so don't show up under 'g list'. */

void Cmd_Rows(const std::vector<std::string>& args);

#endif // GAME_CONSOLE_ROWS_H_
