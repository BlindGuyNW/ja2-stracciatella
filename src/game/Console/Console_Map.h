#ifndef GAME_CONSOLE_MAP_H_
#define GAME_CONSOLE_MAP_H_

#include <string>
#include <vector>

/* Strategic-layer console verbs.
 *
 * `team` covers the mapscreen roster — current state, contracts, location.
 * `map` covers the strategic grid — sectors, towns, mines, militia, intel.
 *
 * Read-only in the first cut. Movement / assignment / contract-extension
 * verbs land in subsequent passes; the design spec lives in
 * docs/mapscreen.md. */

void Cmd_Team  (const std::vector<std::string>& args);
void Cmd_Map   (const std::vector<std::string>& args);
void Cmd_Laptop(const std::vector<std::string>& args);

#endif // GAME_CONSOLE_MAP_H_
