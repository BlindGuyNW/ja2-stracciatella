#ifndef GAME_CONSOLE_QUERY_H_
#define GAME_CONSOLE_QUERY_H_

#include <string>
#include <vector>

/* Read-only query commands. All output is routed through Console_Println.
 * All visibility checks route through ConsoleVis::IsKnownSoldier /
 * ConsoleVis::IsKnownTile so fog-of-war stays in one place. */

struct SOLDIERTYPE;

void Cmd_Sector   (const std::vector<std::string>& args);
void Cmd_Merc     (const std::vector<std::string>& args);

// Compact selected-merc state line: "<name>: life X/Y, breath X/Y, AP N,
// <stance> facing <dir>." plus a held-weapon line. Shared so `select` can
// echo the same prompt the `merc` verb produces.
void PrintMercSummary(const SOLDIERTYPE& s);
void Cmd_Stats    (const std::vector<std::string>& args);
void Cmd_Nearby   (const std::vector<std::string>& args);
void Cmd_Look     (const std::vector<std::string>& args);
void Cmd_Tile     (const std::vector<std::string>& args);
void Cmd_Room     (const std::vector<std::string>& args);
void Cmd_Cth      (const std::vector<std::string>& args);
void Cmd_Cover    (const std::vector<std::string>& args);
void Cmd_Sight    (const std::vector<std::string>& args);
void Cmd_Inventory(const std::vector<std::string>& args);
void Cmd_Examine  (const std::vector<std::string>& args);
void Cmd_Path     (const std::vector<std::string>& args);

#endif // GAME_CONSOLE_QUERY_H_
