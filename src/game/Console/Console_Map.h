#ifndef GAME_CONSOLE_MAP_H_
#define GAME_CONSOLE_MAP_H_

#include <string>
#include <vector>

/* Strategic-layer console verbs.
 *
 * `team` covers the mapscreen roster — current state, contracts, location,
 * multi-select, and sleep toggle.
 * `map` covers the strategic grid — sectors, towns, mines, militia, intel,
 * Z-level, movement plotting, and path cancellation.
 *
 * Bottom-strip verbs (`compress`, `tactical`, `quit`, `log`) sit at top
 * level rather than inside `map` — they're rare-use one-shots. The design
 * spec is docs/mapscreen.md. Assignment / contract / sort / heli /
 * redistribute land in later passes. */

void Cmd_Team    (const std::vector<std::string>& args);
void Cmd_Map     (const std::vector<std::string>& args);
void Cmd_Laptop  (const std::vector<std::string>& args);
void Cmd_Compress(const std::vector<std::string>& args);
void Cmd_Tactical(const std::vector<std::string>& args);
void Cmd_Quit    (const std::vector<std::string>& args);
void Cmd_Log     (const std::vector<std::string>& args);

#endif // GAME_CONSOLE_MAP_H_
