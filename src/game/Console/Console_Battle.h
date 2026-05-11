#ifndef GAME_CONSOLE_BATTLE_H_
#define GAME_CONSOLE_BATTLE_H_

#include <string>
#include <vector>

/* Pre-battle interface (the sector-arrival popup) verb. The dialog
 * itself is mouse-driven and visually rich — sector header, encounter
 * type, defender/enemy counts, three action buttons — none of which a
 * screen-reader user can perceive. `battle` prints a structured
 * summary; `battle go`/`auto`/`retreat` drive the three buttons via
 * the engine's public Activate* entry points in PreBattle_Interface. */
void Cmd_Battle(const std::vector<std::string>& args);

/* Called from InitPreBattleInterface once the dialog is open. Pushes
 * a one-shot AX_Say so the SR user learns the dialog appeared without
 * having to poll, and mirrors the full summary into the transcript. */
void Console_AnnouncePreBattle();

#endif // GAME_CONSOLE_BATTLE_H_
