#ifndef GAME_CONSOLE_WEB_H_
#define GAME_CONSOLE_WEB_H_

#include <string>
#include <vector>

/* Laptop bookmark navigation. The bookmark sidebar's mouse regions are
 * fragile to drive via `g`/`r`, so we expose a direct route into
 * GoToWebPage(): list bookmarked sites or jump to one by name. The
 * "is the site bookmarked yet?" gate matches the in-game UI — sites
 * not in LaptopSaveInfo.iBookMarkList aren't reachable here either. */

void Cmd_Web(const std::vector<std::string>& args);

#endif // GAME_CONSOLE_WEB_H_
