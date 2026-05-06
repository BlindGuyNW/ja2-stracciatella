#ifndef GAME_CONSOLE_CHEAT_H_
#define GAME_CONSOLE_CHEAT_H_

#include <string>
#include <vector>

/* Dev/test cheats for the accessibility console. These force engine state
 * that would otherwise require sighted in-game actions (Alt+click on a
 * laptop icon, etc.) or hours of play to reach. Not intended as a player
 * surface — the namespace exists so a SR developer can iterate on coverage
 * for laptop sites and other late-unlock features without first replaying
 * to the natural unlock event. */

void Cmd_Cheat(const std::vector<std::string>& args);

#endif // GAME_CONSOLE_CHEAT_H_
