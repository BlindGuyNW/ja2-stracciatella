#ifndef GAME_CONSOLE_EMAIL_H_
#define GAME_CONSOLE_EMAIL_H_

#include <string>
#include <vector>

/* Inbox readout sourced from pEmailList in memory (not from rendered
 * text). Strictly gated on LAPTOP_SCREEN — the laptop is the only place
 * the in-game UI exposes mail, and the console mirrors that constraint. */

void Cmd_Email(const std::vector<std::string>& args);

#endif // GAME_CONSOLE_EMAIL_H_
