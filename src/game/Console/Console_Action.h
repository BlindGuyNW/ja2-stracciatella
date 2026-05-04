#ifndef GAME_CONSOLE_ACTION_H_
#define GAME_CONSOLE_ACTION_H_

#include <string>
#include <vector>

/* Action commands. These mutate game state through the same engine entry
 * points the AI uses (e.g. EVENT_FireGun-equivalents), so weapon sound,
 * animation, and downstream ScreenMsg flow happen for free. */

void Cmd_Select  (const std::vector<std::string>& args);
void Cmd_Move    (const std::vector<std::string>& args);
void Cmd_Turn    (const std::vector<std::string>& args);
void Cmd_Stance  (const std::vector<std::string>& args);
void Cmd_Fire    (const std::vector<std::string>& args);
void Cmd_EndTurn (const std::vector<std::string>& args);

#endif // GAME_CONSOLE_ACTION_H_
